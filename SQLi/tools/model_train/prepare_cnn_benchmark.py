#!/usr/bin/env python3
"""Export TextCNN for KADD and reconstruct a token evaluation split.

This utility is intended to run on the Windows/Linux x86 host that owns the
trained ONNX model and the original ``Query, Label`` CSV.  It deliberately
binds only the DFA tokenizer: CNN inputs are padded token IDs, not feature
engine outputs.

The split is a true held-out set only when the supplied ONNX model was trained
with the same filtered rows and split parameters.  For an existing model whose
training provenance is unknown, this dataset is still valid for backend parity
and performance comparisons, but its accuracy must not be presented as an
independently certified generalization result.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import os
from collections import Counter
from pathlib import Path
from typing import Sequence

import numpy as np
import pandas as pd
from sklearn.metrics import accuracy_score, confusion_matrix
from sklearn.model_selection import train_test_split

from kadd_cnn_format import (
    PAD_ID,
    SEQ_DIM,
    assert_export_parity,
    export_kadd_cnn,
    load_kadd_cnn,
)
from kadd_rf_format import export_kadd_dataset


TOKEN_MIN = 0
TOKEN_MAX = PAD_ID - 1
CSV_ENCODINGS = ("utf-8-sig", "utf-8", "gb18030", "gbk", "latin-1")


class _DfaTablePaths(ctypes.Structure):
    _fields_ = (
        ("transition_table_path", ctypes.c_char_p),
        ("output_table_path", ctypes.c_char_p),
        ("eod_table_path", ctypes.c_char_p),
    )


class DfaTokenizer:
    """Minimal, ownership-safe ctypes wrapper around the KADD DFA C API."""

    def __init__(
        self,
        library_path: Path,
        transition_path: Path,
        output_path: Path,
        eod_path: Path,
        *,
        max_input_length: int,
        max_tokens: int,
    ) -> None:
        self._library = ctypes.CDLL(os.fspath(library_path))
        self._library.kadd_dfa_engine_create.argtypes = (
            ctypes.POINTER(ctypes.c_void_p),
            ctypes.POINTER(_DfaTablePaths),
            ctypes.c_size_t,
            ctypes.c_size_t,
        )
        self._library.kadd_dfa_engine_create.restype = ctypes.c_int
        self._library.kadd_dfa_engine_tokenize.argtypes = (
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int),
        )
        self._library.kadd_dfa_engine_tokenize.restype = ctypes.c_int
        # kadd_dfa_engine_free 的签名为 (kadd_dfa_engine_t **)：需传句柄变量的地址
        self._library.kadd_dfa_engine_free.argtypes = (
            ctypes.POINTER(ctypes.c_void_p),)
        self._library.kadd_dfa_engine_free.restype = None

        # Keep the encoded path objects alive through the create call.  The C
        # implementation loads each table eagerly and does not retain them.
        encoded_paths = tuple(
            os.fsencode(os.fspath(path))
            for path in (transition_path, output_path, eod_path)
        )
        table_paths = _DfaTablePaths(*encoded_paths)
        self._engine = ctypes.c_void_p()
        result = self._library.kadd_dfa_engine_create(
            ctypes.byref(self._engine),
            ctypes.byref(table_paths),
            max_input_length,
            max_tokens,
        )
        if result != 0 or not self._engine.value:
            if self._engine.value:
                self._library.kadd_dfa_engine_free(ctypes.byref(self._engine))
            self._engine = ctypes.c_void_p()
            raise RuntimeError(f"DFA engine create failed with error {result}")

        self._max_input_length = max_input_length
        self._max_tokens = max_tokens

    def tokenize(self, query: str) -> tuple[int, np.ndarray]:
        """Return ``(status, tokens)``; status is the original DFA error code."""
        try:
            encoded = query.encode("utf-8")
        except UnicodeEncodeError:
            return -1, np.empty(0, dtype=np.int32)
        if len(encoded) > self._max_input_length:
            # Match the C engine's rejection without emitting one stderr line
            # for every oversized row in a large preparation run.
            return -1000, np.empty(0, dtype=np.int32)

        output = (ctypes.c_int * self._max_tokens)()
        token_count = ctypes.c_int()
        result = self._library.kadd_dfa_engine_tokenize(
            self._engine,
            encoded,
            len(encoded),
            output,
            ctypes.byref(token_count),
        )
        if result != 0:
            return result, np.empty(0, dtype=np.int32)
        if token_count.value < 0 or token_count.value > self._max_tokens:
            raise RuntimeError(
                "DFA tokenizer returned an invalid token count "
                f"{token_count.value} (capacity {self._max_tokens})"
            )
        tokens = np.ctypeslib.as_array(output)[: token_count.value].copy()
        return 0, np.asarray(tokens, dtype=np.int32)

    def close(self) -> None:
        if getattr(self, "_engine", None) is not None and self._engine.value:
            self._library.kadd_dfa_engine_free(ctypes.byref(self._engine))
            self._engine = ctypes.c_void_p()

    def __enter__(self) -> "DfaTokenizer":
        return self

    def __exit__(self, _exc_type, _exc_value, _traceback) -> None:
        self.close()


def _read_csv(path: Path) -> tuple[pd.DataFrame, str]:
    last_error: BaseException | None = None
    for encoding in CSV_ENCODINGS:
        try:
            return pd.read_csv(path, encoding=encoding), encoding
        except (UnicodeDecodeError, UnicodeError) as error:
            last_error = error
    raise ValueError(
        f"unable to decode CSV {path} with {', '.join(CSV_ENCODINGS)}"
    ) from last_error


def _column(frame: pd.DataFrame, requested: str):
    matches = [
        column
        for column in frame.columns
        if str(column).strip().casefold() == requested.casefold()
    ]
    if len(matches) != 1:
        raise ValueError(
            f"CSV must contain exactly one {requested!r} column; "
            f"found {len(matches)}"
        )
    return matches[0]


def _binary_labels(frame: pd.DataFrame, label_column) -> np.ndarray:
    try:
        numeric = pd.to_numeric(frame[label_column], errors="raise").to_numpy(
            dtype=np.float64, copy=True
        )
    except (TypeError, ValueError) as error:
        raise ValueError("Label column must contain only binary values 0 or 1") from error
    if (
        numeric.ndim != 1
        or not np.all(np.isfinite(numeric))
        or not np.all((numeric == 0.0) | (numeric == 1.0))
    ):
        raise ValueError("Label column must contain only binary values 0 or 1")
    return numeric.astype(np.int64, copy=False)


def load_token_sequences(
    data_path: Path,
    tokenizer: DfaTokenizer,
) -> tuple[np.ndarray, np.ndarray, dict[str, object]]:
    """Tokenize valid CSV rows and return fixed 300-wide float32 sequences."""
    frame, encoding = _read_csv(data_path)
    query_column = _column(frame, "Query")
    label_column = _column(frame, "Label")
    labels = _binary_labels(frame, label_column)

    sequences: list[np.ndarray] = []
    accepted_labels: list[int] = []
    empty_count = 0
    failed_count = 0
    failure_codes: Counter[int] = Counter()

    for row_offset, (raw_query, label) in enumerate(
        zip(frame[query_column].array, labels), start=2
    ):
        if pd.isna(raw_query):
            empty_count += 1
            continue
        query = str(raw_query)
        if not query.strip():
            empty_count += 1
            continue

        status, tokens = tokenizer.tokenize(query)
        if status != 0:
            failed_count += 1
            failure_codes[status] += 1
            continue
        if tokens.size == 0:
            empty_count += 1
            continue
        if np.any(tokens < TOKEN_MIN) or np.any(tokens > TOKEN_MAX):
            bad = int(tokens[(tokens < TOKEN_MIN) | (tokens > TOKEN_MAX)][0])
            raise ValueError(
                f"CSV row {row_offset}: DFA emitted token {bad}; "
                f"CNN token IDs must be in [{TOKEN_MIN}, {TOKEN_MAX}] "
                f"and {PAD_ID} is reserved for padding"
            )

        sequence = np.full(SEQ_DIM, PAD_ID, dtype=np.float32)
        copied = min(int(tokens.size), SEQ_DIM)
        sequence[:copied] = tokens[:copied]
        sequences.append(sequence)
        accepted_labels.append(int(label))

        processed = row_offset - 1
        if processed % 1000 == 0:
            print(
                f"tokenize progress: {processed}/{len(frame)} "
                f"(valid={len(sequences)}, skipped={empty_count + failed_count})"
            )

    if not sequences:
        raise ValueError("no valid tokenized samples remain after filtering")

    features = np.ascontiguousarray(sequences, dtype=np.float32)
    targets = np.asarray(accepted_labels, dtype=np.int64)
    if features.shape != (len(sequences), SEQ_DIM):
        raise AssertionError("internal CNN sequence shape mismatch")
    if not np.all((targets == 0) | (targets == 1)):
        raise AssertionError("internal label validation mismatch")

    statistics: dict[str, object] = {
        "encoding": encoding,
        "rows": len(frame),
        "valid": len(features),
        "empty": empty_count,
        "failed": failed_count,
        "failure_codes": dict(sorted(failure_codes.items())),
    }
    return features, targets, statistics


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def _predict_in_batches(model, features: np.ndarray, batch_size: int = 128) -> np.ndarray:
    """Bound the NumPy reference path's temporary convolution tensors."""
    result = np.empty((len(features), 2), dtype=np.float32)
    for start in range(0, len(features), batch_size):
        stop = min(start + batch_size, len(features))
        result[start:stop] = model.predict_proba(features[start:stop])
    return result


def _require_input_file(parser: argparse.ArgumentParser, path: Path, option: str) -> None:
    if not path.is_file():
        parser.error(f"{option} is not a file: {path}")


def _parse_arguments(argv: Sequence[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Convert the fixed TextCNN ONNX model to .kcn and create its "
            "stratified 300-token KADD evaluation dataset"
        )
    )
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--dfa-lib", type=Path, required=True)
    parser.add_argument("--transition", type=Path, required=True)
    parser.add_argument("--output-table", type=Path, required=True)
    parser.add_argument("--eod", type=Path, required=True)
    parser.add_argument("--onnx-model", type=Path, required=True)
    parser.add_argument("--native-model", type=Path, required=True)
    parser.add_argument("--test-data", type=Path, required=True)
    parser.add_argument("--test-size", type=float, default=0.2)
    parser.add_argument("--split-seed", type=int, default=12)
    parser.add_argument("--max-input-length", type=int, default=300)
    parser.add_argument("--max-tokens", type=int, default=300)
    parser.add_argument("--parity-samples", type=int, default=128)
    arguments = parser.parse_args(argv)

    if not 0.0 < arguments.test_size < 1.0:
        parser.error("--test-size must be between 0 and 1")
    if arguments.max_input_length <= 0:
        parser.error("--max-input-length must be positive")
    if arguments.max_tokens <= 0:
        parser.error("--max-tokens must be positive")
    if arguments.parity_samples <= 0:
        parser.error("--parity-samples must be positive")
    for option, path in (
        ("--data", arguments.data),
        ("--dfa-lib", arguments.dfa_lib),
        ("--transition", arguments.transition),
        ("--output-table", arguments.output_table),
        ("--eod", arguments.eod),
        ("--onnx-model", arguments.onnx_model),
    ):
        _require_input_file(parser, path, option)

    input_paths = {
        path.resolve()
        for path in (
            arguments.data,
            arguments.dfa_lib,
            arguments.transition,
            arguments.output_table,
            arguments.eod,
            arguments.onnx_model,
        )
    }
    output_paths = (
        arguments.native_model.resolve(),
        arguments.test_data.resolve(),
    )
    if len(set(output_paths)) != len(output_paths):
        parser.error("--native-model and --test-data must be different files")
    if any(path in input_paths for path in output_paths):
        parser.error("output paths must not overwrite any input file")
    if arguments.onnx_model.suffix.casefold() != ".onnx":
        parser.error("--onnx-model must have the .onnx extension")
    if arguments.native_model.suffix.casefold() != ".kcn":
        parser.error("--native-model must have the .kcn extension")
    if arguments.test_data.suffix.casefold() != ".kds":
        parser.error("--test-data must have the .kds extension")
    return arguments


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parse_arguments(argv)

    export_kadd_cnn(arguments.onnx_model, arguments.native_model)
    native_model = load_kadd_cnn(arguments.native_model)

    with DfaTokenizer(
        arguments.dfa_lib,
        arguments.transition,
        arguments.output_table,
        arguments.eod,
        max_input_length=arguments.max_input_length,
        max_tokens=arguments.max_tokens,
    ) as tokenizer:
        features, labels, statistics = load_token_sequences(
            arguments.data, tokenizer
        )

    train_x, test_x, train_y, test_y = train_test_split(
        features,
        labels,
        test_size=arguments.test_size,
        random_state=arguments.split_seed,
        stratify=labels,
    )
    export_kadd_dataset(test_x, test_y, arguments.test_data)

    probabilities = _predict_in_batches(native_model, test_x)
    predicted = np.argmax(probabilities, axis=1)
    accuracy = float(accuracy_score(test_y, predicted))
    matrix = confusion_matrix(test_y, predicted, labels=[0, 1])

    parity_count = min(len(test_x), arguments.parity_samples)
    maximum_error = assert_export_parity(
        arguments.onnx_model,
        arguments.native_model,
        test_x[:parity_count],
    )

    print(f"CSV encoding:           {statistics['encoding']}")
    print(f"CSV rows:               {statistics['rows']}")
    print(f"valid samples:          {statistics['valid']}")
    print(f"skipped empty:          {statistics['empty']}")
    print(f"skipped DFA failures:   {statistics['failed']}")
    if statistics["failure_codes"]:
        print(f"DFA failure codes:      {statistics['failure_codes']}")
    print(f"train samples:          {len(train_x)}")
    print(f"evaluation samples:     {len(test_x)}")
    print(f"evaluation class count: 0={int(np.sum(test_y == 0))}, "
          f"1={int(np.sum(test_y == 1))}")
    print(
        f"split parameters:       test_size={arguments.test_size:g}, "
        f"seed={arguments.split_seed}, "
        f"max_input={arguments.max_input_length}, "
        f"max_tokens={arguments.max_tokens}"
    )
    print(f"test accuracy:          {accuracy:.6f}")
    print("confusion matrix [rows=true, columns=predicted]:")
    print(matrix)
    print(
        f"ONNX/native parity:     max error={maximum_error:.3e} "
        f"({parity_count} samples)"
    )
    print(
        "provenance note: this reconstructed split certifies backend parity; "
        "it is a true held-out accuracy set only if the ONNX training run used "
        "the same filtered data and split parameters"
    )

    for label, path in (
        ("source CSV", arguments.data),
        ("DFA library", arguments.dfa_lib),
        ("DFA transition table", arguments.transition),
        ("DFA output table", arguments.output_table),
        ("DFA EOD table", arguments.eod),
        ("ONNX model", arguments.onnx_model),
        ("native model", arguments.native_model),
        ("test data", arguments.test_data),
    ):
        print(f"{label}: {path.resolve()}")
        print(f"  SHA256: {_sha256(path)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

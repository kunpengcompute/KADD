#!/usr/bin/env python3
"""Train a KADD-RF model and create its strictly held-out benchmark dataset.

This utility consumes the repository's precomputed training_vectors.csv, so it
does not need the DFA/feature shared libraries, PyTorch, ONNX, or ONNX Runtime.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import classification_report, confusion_matrix
from sklearn.model_selection import train_test_split

from kadd_rf_format import (
    assert_export_parity,
    export_kadd_dataset,
    export_kadd_rf,
)


DEFAULT_RF_PARAMS = {
    "n_estimators": 10,
    "max_depth": 8,
    "min_samples_split": 2,
    "min_samples_leaf": 1,
    "n_jobs": -1,
    "random_state": 42,
    "class_weight": "balanced",
}


def load_vectors(path: Path) -> tuple[np.ndarray, np.ndarray]:
    frame = pd.read_csv(path, encoding="utf-8-sig")
    lowered = {str(column).lower(): column for column in frame.columns}
    if "label" not in lowered:
        raise ValueError("vectors CSV has no Label column")

    feature_columns = []
    for index in range(28):
        name = f"feature_{index}"
        if name not in lowered:
            raise ValueError(f"vectors CSV has no {name} column")
        feature_columns.append(lowered[name])

    features = frame[feature_columns].to_numpy(dtype=np.float32, copy=True)
    labels = frame[lowered["label"]].to_numpy(dtype=np.int64, copy=True)
    if features.shape[0] == 0:
        raise ValueError("vectors CSV is empty")
    if not np.all(np.isfinite(features)):
        raise ValueError("vectors CSV contains NaN or infinity")
    if not np.all((labels == 0) | (labels == 1)):
        raise ValueError("labels must be binary (0 or 1)")
    return features, labels


def main() -> int:
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(
        description="Train native KADD-RF and export an unbiased test dataset"
    )
    parser.add_argument(
        "--vectors",
        type=Path,
        default=script_dir / "train_data" / "training_vectors.csv",
    )
    parser.add_argument(
        "--model",
        type=Path,
        default=script_dir / "../../assets/model/model_tokens_rf.krf",
    )
    parser.add_argument(
        "--test-data",
        type=Path,
        default=script_dir / "../../assets/model/model_tokens_rf.test.kds",
    )
    parser.add_argument(
        "--onnx-model",
        type=Path,
        default=None,
        help="optionally export the same fitted forest as ONNX for a fair runtime comparison",
    )
    parser.add_argument("--test-size", type=float, default=0.2)
    parser.add_argument("--split-seed", type=int, default=12)
    parser.add_argument("--trees", type=int, default=10)
    parser.add_argument("--max-depth", type=int, default=8)
    args = parser.parse_args()

    if not 0.0 < args.test_size < 1.0:
        parser.error("--test-size must be between 0 and 1")
    if args.trees <= 0 or args.max_depth <= 0:
        parser.error("--trees and --max-depth must be positive")

    features, labels = load_vectors(args.vectors)
    train_x, test_x, train_y, test_y = train_test_split(
        features,
        labels,
        test_size=args.test_size,
        random_state=args.split_seed,
        stratify=labels,
    )

    parameters = dict(DEFAULT_RF_PARAMS)
    parameters["n_estimators"] = args.trees
    parameters["max_depth"] = args.max_depth
    model = RandomForestClassifier(**parameters)
    model.fit(train_x, train_y)
    predicted = model.predict(test_x)

    export_kadd_rf(model, args.model)
    export_kadd_dataset(test_x, test_y, args.test_data)
    if args.onnx_model is not None:
        try:
            from skl2onnx import convert_sklearn
            from skl2onnx.common.data_types import FloatTensorType
        except ImportError as error:
            raise RuntimeError(
                "--onnx-model requires skl2onnx: "
                "python3 -m pip install skl2onnx onnx"
            ) from error
        args.onnx_model.parent.mkdir(parents=True, exist_ok=True)
        onnx_model = convert_sklearn(
            model,
            initial_types=[("input", FloatTensorType([None, features.shape[1]]))],
            target_opset=12,
            options={id(model): {"zipmap": False}},
        )
        args.onnx_model.write_bytes(onnx_model.SerializeToString())
    maximum_error = assert_export_parity(
        model, args.model, test_x[: min(len(test_x), 4096)]
    )

    print(f"train samples: {len(train_x)}")
    print(f"test samples:  {len(test_x)}")
    print(f"model:         {args.model.resolve()}")
    print(f"test data:     {args.test_data.resolve()}")
    if args.onnx_model is not None:
        print(f"ONNX model:    {args.onnx_model.resolve()}")
    print(f"export parity max error: {maximum_error:.3e}")
    print(classification_report(test_y, predicted, digits=6))
    print("confusion matrix:")
    print(confusion_matrix(test_y, predicted))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""KADD-CNN v1 portable TextCNN model exporter and reference reader.

The file format is deliberately fixed-width and little-endian so a model
produced on an x86 training host can be consumed unchanged by the lightweight
C runtime on x86-64 or AArch64/SVE.

Only the SQLi project's fixed TextCNN graph is accepted:

    float32[batch, 300]
      -> Cast(int64) -> Embedding(29, 32) -> Transpose
      -> {Conv1d(32, 64, k), ReLU, ReduceMax(axis=2)} for k=2,3,4,5
      -> Concat(256) -> Gemm(256, 64) -> ReLU
      -> Gemm(64, 2) -> Softmax

Keeping the accepted graph narrow is intentional.  It prevents an apparently
compatible ONNX model with different padding, strides, activations, or Gemm
semantics from being silently exported with incorrect native behavior.
"""

from __future__ import annotations

import argparse
import os
import struct
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Sequence

import numpy as np


MAGIC = b"KADDCN1\0"
VERSION = 1
HEADER = struct.Struct("<8sIIIIIIIIIIQII")

SEQ_DIM = 300
VOCAB_SIZE = 29
EMBED_DIM = 32
CHANNELS = 64
KERNELS = (2, 3, 4, 5)
BRANCH_COUNT = len(KERNELS)
HIDDEN_SIZE = 64
CLASS_COUNT = 2
PAD_ID = 28
FLAGS = 0

_INITIALIZER_SHAPES = {
    "m.emb.weight": (VOCAB_SIZE, EMBED_DIM),
    **{
        f"m.convs.{index}.weight": (CHANNELS, EMBED_DIM, kernel)
        for index, kernel in enumerate(KERNELS)
    },
    **{
        f"m.convs.{index}.bias": (CHANNELS,)
        for index in range(BRANCH_COUNT)
    },
    "m.fc1.weight": (HIDDEN_SIZE, CHANNELS * BRANCH_COUNT),
    "m.fc1.bias": (HIDDEN_SIZE,),
    "m.fc2.weight": (CLASS_COUNT, HIDDEN_SIZE),
    "m.fc2.bias": (CLASS_COUNT,),
}

PAYLOAD_FLOAT_COUNT = (
    VOCAB_SIZE * EMBED_DIM
    + sum(CHANNELS * EMBED_DIM * kernel + CHANNELS for kernel in KERNELS)
    + HIDDEN_SIZE * CHANNELS * BRANCH_COUNT
    + HIDDEN_SIZE
    + CLASS_COUNT * HIDDEN_SIZE
    + CLASS_COUNT
)
PAYLOAD_SIZE = PAYLOAD_FLOAT_COUNT * np.dtype("<f4").itemsize

if HEADER.size != 64:  # Defensive assertion: this is part of the ABI.
    raise AssertionError(f"KADD-CNN header must be 64 bytes, got {HEADER.size}")


def _require_onnx():
    try:
        import onnx
    except ImportError as exc:
        raise RuntimeError(
            "exporting KADD-CNN requires the 'onnx' package; "
            "install it with: python -m pip install onnx"
        ) from exc
    return onnx


def _attributes(onnx, node) -> dict[str, Any]:
    return {
        attribute.name: onnx.helper.get_attribute_value(attribute)
        for attribute in node.attribute
    }


def _expect_attributes(
    onnx,
    node,
    expected: dict[str, Any],
    *,
    context: str,
) -> None:
    actual = _attributes(onnx, node)
    if set(actual) != set(expected):
        raise ValueError(
            f"{context}: attributes {sorted(actual)} do not match "
            f"expected {sorted(expected)}"
        )
    for name, expected_value in expected.items():
        actual_value = actual[name]
        if isinstance(expected_value, (list, tuple)):
            if list(actual_value) != list(expected_value):
                raise ValueError(
                    f"{context}: attribute {name}={actual_value!r}, "
                    f"expected {expected_value!r}"
                )
        elif isinstance(expected_value, float):
            if float(actual_value) != expected_value:
                raise ValueError(
                    f"{context}: attribute {name}={actual_value!r}, "
                    f"expected {expected_value!r}"
                )
        elif int(actual_value) != int(expected_value):
            raise ValueError(
                f"{context}: attribute {name}={actual_value!r}, "
                f"expected {expected_value!r}"
            )


def _expect_node(
    onnx,
    node,
    op_type: str,
    inputs: Sequence[str],
    attributes: dict[str, Any],
    *,
    context: str,
) -> str:
    if node.domain not in ("", "ai.onnx"):
        raise ValueError(f"{context}: unsupported node domain {node.domain!r}")
    if node.op_type != op_type:
        raise ValueError(
            f"{context}: operator is {node.op_type!r}, expected {op_type!r}"
        )
    if list(node.input) != list(inputs):
        raise ValueError(
            f"{context}: inputs {list(node.input)!r} do not match "
            f"expected {list(inputs)!r}"
        )
    if len(node.output) != 1 or not node.output[0]:
        raise ValueError(f"{context}: exactly one named output is required")
    _expect_attributes(onnx, node, attributes, context=context)
    return node.output[0]


def _validate_value_info(
    onnx,
    value_info,
    *,
    name: str,
    width: int,
    context: str,
) -> None:
    if value_info.name != name:
        raise ValueError(
            f"{context}: tensor is named {value_info.name!r}, expected {name!r}"
        )
    tensor_type = value_info.type.tensor_type
    if tensor_type.elem_type != onnx.TensorProto.FLOAT:
        raise ValueError(f"{context}: tensor must have float32 element type")
    dimensions = tensor_type.shape.dim
    if len(dimensions) != 2:
        raise ValueError(f"{context}: tensor must be rank 2")
    batch = dimensions[0]
    if batch.HasField("dim_value") and batch.dim_value != 0:
        raise ValueError(f"{context}: batch dimension must be dynamic")
    feature = dimensions[1]
    if not feature.HasField("dim_value") or feature.dim_value != width:
        raise ValueError(
            f"{context}: second dimension must be the fixed value {width}"
        )


def _validate_graph_and_extract(model) -> dict[str, np.ndarray]:
    """Validate the exact supported graph and return its float32 parameters."""
    onnx = _require_onnx()
    onnx.checker.check_model(model)

    standard_opsets = [
        int(imported.version)
        for imported in model.opset_import
        if imported.domain in ("", "ai.onnx")
    ]
    other_opsets = [
        imported.domain
        for imported in model.opset_import
        if imported.domain not in ("", "ai.onnx")
    ]
    if standard_opsets != [13] or other_opsets:
        raise ValueError(
            "KADD-CNN requires exactly the standard ONNX opset 13 "
            f"(got standard={standard_opsets}, other={other_opsets})"
        )

    graph = model.graph
    if len(graph.input) != 1:
        raise ValueError("KADD-CNN requires exactly one graph input")
    if len(graph.output) != 1:
        raise ValueError("KADD-CNN requires exactly one graph output")
    _validate_value_info(
        onnx,
        graph.input[0],
        name="input",
        width=SEQ_DIM,
        context="graph input",
    )
    _validate_value_info(
        onnx,
        graph.output[0],
        name="prob",
        width=CLASS_COUNT,
        context="graph output",
    )
    if graph.sparse_initializer:
        raise ValueError("KADD-CNN does not support sparse initializers")

    initializers = {initializer.name: initializer for initializer in graph.initializer}
    if len(initializers) != len(graph.initializer):
        raise ValueError("ONNX graph contains duplicate initializer names")
    if set(initializers) != set(_INITIALIZER_SHAPES):
        missing = sorted(set(_INITIALIZER_SHAPES) - set(initializers))
        extra = sorted(set(initializers) - set(_INITIALIZER_SHAPES))
        raise ValueError(
            f"unexpected initializer set (missing={missing}, extra={extra})"
        )

    parameters: dict[str, np.ndarray] = {}
    for name, expected_shape in _INITIALIZER_SHAPES.items():
        tensor = initializers[name]
        if tensor.data_type != onnx.TensorProto.FLOAT:
            raise ValueError(f"initializer {name!r} must contain float32 values")
        if tuple(tensor.dims) != expected_shape:
            raise ValueError(
                f"initializer {name!r} has shape {tuple(tensor.dims)}, "
                f"expected {expected_shape}"
            )
        array = np.asarray(onnx.numpy_helper.to_array(tensor))
        if array.dtype != np.float32:
            raise ValueError(
                f"initializer {name!r} decoded as {array.dtype}, expected float32"
            )
        if not np.all(np.isfinite(array)):
            raise ValueError(f"initializer {name!r} contains NaN or infinity")
        parameters[name] = np.ascontiguousarray(array, dtype="<f4")

    if not np.array_equal(
        parameters["m.emb.weight"][PAD_ID],
        np.zeros(EMBED_DIM, dtype=np.float32),
    ):
        raise ValueError(
            f"embedding row padding_idx={PAD_ID} must contain exact zeros"
        )

    nodes = list(graph.node)
    expected_node_count = 3 + BRANCH_COUNT * 3 + 1 + 3 + 1
    if len(nodes) != expected_node_count:
        raise ValueError(
            f"KADD-CNN graph must contain {expected_node_count} nodes, "
            f"got {len(nodes)}"
        )

    cursor = 0
    cast = _expect_node(
        onnx,
        nodes[cursor],
        "Cast",
        ["input"],
        {"to": onnx.TensorProto.INT64},
        context="input Cast",
    )
    cursor += 1
    gather = _expect_node(
        onnx,
        nodes[cursor],
        "Gather",
        ["m.emb.weight", cast],
        {},
        context="Embedding Gather",
    )
    cursor += 1
    transposed = _expect_node(
        onnx,
        nodes[cursor],
        "Transpose",
        [gather],
        {"perm": [0, 2, 1]},
        context="Embedding Transpose",
    )
    cursor += 1

    pooled_outputs: list[str] = []
    for branch, kernel in enumerate(KERNELS):
        convolution = _expect_node(
            onnx,
            nodes[cursor],
            "Conv",
            [
                transposed,
                f"m.convs.{branch}.weight",
                f"m.convs.{branch}.bias",
            ],
            {
                "dilations": [1],
                "group": 1,
                "kernel_shape": [kernel],
                "pads": [0, 0],
                "strides": [1],
            },
            context=f"Conv branch k={kernel}",
        )
        cursor += 1
        activated = _expect_node(
            onnx,
            nodes[cursor],
            "Relu",
            [convolution],
            {},
            context=f"ReLU branch k={kernel}",
        )
        cursor += 1
        pooled_outputs.append(
            _expect_node(
                onnx,
                nodes[cursor],
                "ReduceMax",
                [activated],
                {"axes": [2], "keepdims": 0},
                context=f"global max branch k={kernel}",
            )
        )
        cursor += 1

    concatenated = _expect_node(
        onnx,
        nodes[cursor],
        "Concat",
        pooled_outputs,
        {"axis": 1},
        context="branch Concat",
    )
    cursor += 1
    hidden = _expect_node(
        onnx,
        nodes[cursor],
        "Gemm",
        [concatenated, "m.fc1.weight", "m.fc1.bias"],
        {"alpha": 1.0, "beta": 1.0, "transB": 1},
        context="fc1 Gemm",
    )
    cursor += 1
    hidden = _expect_node(
        onnx,
        nodes[cursor],
        "Relu",
        [hidden],
        {},
        context="fc1 ReLU",
    )
    cursor += 1
    logits = _expect_node(
        onnx,
        nodes[cursor],
        "Gemm",
        [hidden, "m.fc2.weight", "m.fc2.bias"],
        {"alpha": 1.0, "beta": 1.0, "transB": 1},
        context="fc2 Gemm",
    )
    cursor += 1
    probability = _expect_node(
        onnx,
        nodes[cursor],
        "Softmax",
        [logits],
        {"axis": 1},
        context="output Softmax",
    )
    if probability != "prob":
        raise ValueError(
            f"Softmax output is {probability!r}, expected graph output 'prob'"
        )

    return parameters


def _ordered_parameter_arrays(
    parameters: dict[str, np.ndarray],
) -> list[np.ndarray]:
    arrays = [parameters["m.emb.weight"]]
    for branch in range(BRANCH_COUNT):
        arrays.append(parameters[f"m.convs.{branch}.weight"])
        arrays.append(parameters[f"m.convs.{branch}.bias"])
    arrays.extend(
        (
            parameters["m.fc1.weight"],
            parameters["m.fc1.bias"],
            parameters["m.fc2.weight"],
            parameters["m.fc2.bias"],
        )
    )
    return arrays


def export_kadd_cnn(
    onnx_path: os.PathLike[str] | str,
    output_path: os.PathLike[str] | str,
) -> Path:
    """Validate an ONNX TextCNN and export it as a KADD-CNN v1 model."""
    onnx = _require_onnx()
    source = Path(onnx_path)
    model = onnx.load(str(source), load_external_data=True)
    parameters = _validate_graph_and_extract(model)

    payload = b"".join(
        np.ascontiguousarray(array, dtype="<f4").tobytes(order="C")
        for array in _ordered_parameter_arrays(parameters)
    )
    if len(payload) != PAYLOAD_SIZE:
        raise AssertionError(
            f"internal payload size mismatch: {len(payload)} != {PAYLOAD_SIZE}"
        )
    checksum = zlib.crc32(payload) & 0xFFFFFFFF
    header = HEADER.pack(
        MAGIC,
        VERSION,
        HEADER.size,
        SEQ_DIM,
        VOCAB_SIZE,
        EMBED_DIM,
        CHANNELS,
        BRANCH_COUNT,
        HIDDEN_SIZE,
        CLASS_COUNT,
        PAD_ID,
        len(payload),
        checksum,
        FLAGS,
    )

    output = Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)
    file_descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", suffix=".tmp", dir=output.parent
    )
    try:
        with os.fdopen(file_descriptor, "wb") as stream:
            stream.write(header)
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, output)
    except BaseException:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise
    return output


@dataclass(frozen=True)
class PortableCNN:
    """NumPy reference representation of a validated KADD-CNN v1 model."""

    embedding: np.ndarray
    conv_weights: tuple[np.ndarray, ...]
    conv_biases: tuple[np.ndarray, ...]
    fc1_weight: np.ndarray
    fc1_bias: np.ndarray
    fc2_weight: np.ndarray
    fc2_bias: np.ndarray

    def _tokens(self, features: np.ndarray) -> np.ndarray:
        matrix = np.asarray(features)
        if matrix.ndim != 2 or matrix.shape[1] != SEQ_DIM:
            raise ValueError(
                f"expected input shape (batch, {SEQ_DIM}), got {matrix.shape}"
            )
        if not np.issubdtype(matrix.dtype, np.number):
            raise ValueError("input tokens must be numeric")
        if not np.all(np.isfinite(matrix)):
            raise ValueError("input tokens contain NaN or infinity")

        # The ONNX graph contains Cast(float -> int64), but KADD's public CNN
        # token contract is deliberately stricter: malformed fractional IDs
        # are rejected instead of being silently truncated.
        truncated = np.trunc(matrix)
        if np.any(truncated != matrix):
            raise ValueError("input token IDs must be integer-valued")
        tokens = truncated.astype(np.int64, copy=False)
        if np.any(tokens < 0) or np.any(tokens >= VOCAB_SIZE):
            raise ValueError(
                f"token IDs after int64 cast must be in [0, {VOCAB_SIZE - 1}]"
            )
        return tokens

    def predict_logits(self, features: np.ndarray) -> np.ndarray:
        """Run the unfused TextCNN reference path and return float32 logits."""
        tokens = self._tokens(features)
        if tokens.shape[0] == 0:
            return np.empty((0, CLASS_COUNT), dtype=np.float32)

        pooled_branches: list[np.ndarray] = []
        for kernel, weight, bias in zip(
            KERNELS, self.conv_weights, self.conv_biases
        ):
            positions = SEQ_DIM - kernel + 1
            convolution = np.broadcast_to(
                bias.reshape(1, 1, CHANNELS),
                (tokens.shape[0], positions, CHANNELS),
            ).copy()
            for kernel_index in range(kernel):
                # With only 29 possible tokens, fold Embedding and this kernel
                # position into a compact [token, output_channel] table. This
                # is algebraically identical to ONNX cross-correlation and
                # mirrors the C runtime's load-time transformation.
                contributions = (
                    self.embedding @ weight[:, :, kernel_index].T
                )
                convolution += contributions[
                    tokens[
                        :,
                        kernel_index : kernel_index + positions,
                    ]
                ]
            np.maximum(convolution, np.float32(0.0), out=convolution)
            pooled_branches.append(np.max(convolution, axis=1))

        concatenated = np.concatenate(pooled_branches, axis=1)
        hidden = concatenated @ self.fc1_weight.T + self.fc1_bias
        hidden = np.maximum(hidden, np.float32(0.0))
        logits = hidden @ self.fc2_weight.T + self.fc2_bias
        return np.asarray(logits, dtype=np.float32)

    def predict_proba(self, features: np.ndarray) -> np.ndarray:
        """Run TextCNN and return stable two-class float32 probabilities."""
        logits = self.predict_logits(features)
        if logits.shape[0] == 0:
            return logits
        shifted = logits - np.max(logits, axis=1, keepdims=True)
        exponentials = np.exp(shifted)
        return np.asarray(
            exponentials / np.sum(exponentials, axis=1, keepdims=True),
            dtype=np.float32,
        )


def _take_array(
    payload: bytes,
    offset: int,
    shape: Sequence[int],
) -> tuple[np.ndarray, int]:
    count = int(np.prod(shape, dtype=np.int64))
    byte_count = count * np.dtype("<f4").itemsize
    end = offset + byte_count
    if end > len(payload):
        raise ValueError("truncated KADD-CNN parameter payload")
    array = np.frombuffer(payload, dtype="<f4", count=count, offset=offset)
    array = array.reshape(tuple(shape))
    return array, end


def load_kadd_cnn(path: os.PathLike[str] | str) -> PortableCNN:
    """Load and validate a KADD-CNN v1 model for reference inference."""
    data = Path(path).read_bytes()
    if len(data) < HEADER.size:
        raise ValueError("truncated KADD-CNN model header")
    (
        magic,
        version,
        header_size,
        sequence_length,
        vocabulary_size,
        embedding_size,
        channels,
        branch_count,
        hidden_size,
        class_count,
        padding_id,
        payload_size,
        checksum,
        flags,
    ) = HEADER.unpack_from(data)
    if (
        magic != MAGIC
        or version != VERSION
        or header_size != HEADER.size
        or sequence_length != SEQ_DIM
        or vocabulary_size != VOCAB_SIZE
        or embedding_size != EMBED_DIM
        or channels != CHANNELS
        or branch_count != BRANCH_COUNT
        or hidden_size != HIDDEN_SIZE
        or class_count != CLASS_COUNT
        or padding_id != PAD_ID
        or flags != FLAGS
    ):
        raise ValueError("unsupported KADD-CNN header")
    if payload_size != PAYLOAD_SIZE:
        raise ValueError(
            f"invalid KADD-CNN payload size {payload_size}, "
            f"expected {PAYLOAD_SIZE}"
        )

    payload = data[HEADER.size :]
    if len(payload) != payload_size:
        raise ValueError(
            f"truncated or trailing KADD-CNN payload: "
            f"got {len(payload)}, expected {payload_size}"
        )
    if zlib.crc32(payload) & 0xFFFFFFFF != checksum:
        raise ValueError("KADD-CNN payload CRC32 mismatch")

    cursor = 0
    embedding, cursor = _take_array(
        payload, cursor, (VOCAB_SIZE, EMBED_DIM)
    )
    conv_weights: list[np.ndarray] = []
    conv_biases: list[np.ndarray] = []
    for kernel in KERNELS:
        weight, cursor = _take_array(
            payload, cursor, (CHANNELS, EMBED_DIM, kernel)
        )
        bias, cursor = _take_array(payload, cursor, (CHANNELS,))
        conv_weights.append(weight)
        conv_biases.append(bias)
    fc1_weight, cursor = _take_array(
        payload, cursor, (HIDDEN_SIZE, CHANNELS * BRANCH_COUNT)
    )
    fc1_bias, cursor = _take_array(payload, cursor, (HIDDEN_SIZE,))
    fc2_weight, cursor = _take_array(
        payload, cursor, (CLASS_COUNT, HIDDEN_SIZE)
    )
    fc2_bias, cursor = _take_array(payload, cursor, (CLASS_COUNT,))
    if cursor != len(payload):
        raise AssertionError("internal KADD-CNN payload parser mismatch")

    arrays = [
        embedding,
        *conv_weights,
        *conv_biases,
        fc1_weight,
        fc1_bias,
        fc2_weight,
        fc2_bias,
    ]
    if any(not np.all(np.isfinite(array)) for array in arrays):
        raise ValueError("KADD-CNN payload contains NaN or infinity")
    if not np.array_equal(
        embedding[PAD_ID], np.zeros(EMBED_DIM, dtype=np.float32)
    ):
        raise ValueError(
            f"KADD-CNN embedding row padding_idx={PAD_ID} is not zero"
        )
    for array in arrays:
        array.setflags(write=False)

    return PortableCNN(
        embedding=embedding,
        conv_weights=tuple(conv_weights),
        conv_biases=tuple(conv_biases),
        fc1_weight=fc1_weight,
        fc1_bias=fc1_bias,
        fc2_weight=fc2_weight,
        fc2_bias=fc2_bias,
    )


def assert_export_parity(
    onnx_path: os.PathLike[str] | str,
    model_path: os.PathLike[str] | str,
    samples: np.ndarray,
    *,
    atol: float = 2e-5,
    rtol: float = 2e-5,
) -> float:
    """Compare the portable NumPy path with ONNX's reference evaluator."""
    onnx = _require_onnx()
    from onnx.reference import ReferenceEvaluator

    source_model = onnx.load(str(onnx_path), load_external_data=True)
    _validate_graph_and_extract(source_model)
    matrix = np.ascontiguousarray(samples, dtype=np.float32)
    expected = np.asarray(
        ReferenceEvaluator(source_model).run(None, {"input": matrix})[0],
        dtype=np.float32,
    )
    actual = load_kadd_cnn(model_path).predict_proba(matrix)
    maximum_error = float(
        np.max(np.abs(expected - actual), initial=np.float32(0.0))
    )
    if not np.allclose(expected, actual, atol=atol, rtol=rtol):
        raise AssertionError(
            "ONNX/KADD-CNN parity failed: "
            f"max error={maximum_error:.3e}, atol={atol:.1e}, rtol={rtol:.1e}"
        )
    return maximum_error


def _verification_samples(count: int) -> np.ndarray:
    rng = np.random.default_rng(0x4B414444)
    tokens = rng.integers(
        0, VOCAB_SIZE - 1, size=(count, SEQ_DIM), dtype=np.int64
    )
    # Exercise the padding path with different effective sequence lengths.
    if count:
        lengths = rng.integers(0, SEQ_DIM + 1, size=count)
        for row, length in enumerate(lengths):
            tokens[row, length:] = PAD_ID
    return np.asarray(tokens, dtype=np.float32)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Export the fixed SQLi TextCNN ONNX model as KADD-CNN v1"
    )
    parser.add_argument("input", help="input TextCNN ONNX model")
    parser.add_argument("output", help="output portable .kcn model")
    parser.add_argument(
        "--verify-samples",
        type=int,
        default=0,
        metavar="N",
        help="compare N deterministic samples with ONNX ReferenceEvaluator",
    )
    arguments = parser.parse_args(argv)
    if arguments.verify_samples < 0:
        parser.error("--verify-samples must be non-negative")

    result = export_kadd_cnn(arguments.input, arguments.output)
    print(
        f"[OK] KADD-CNN saved: {result} "
        f"({HEADER.size + PAYLOAD_SIZE} bytes, payload={PAYLOAD_SIZE})"
    )
    if arguments.verify_samples:
        samples = _verification_samples(arguments.verify_samples)
        maximum_error = assert_export_parity(
            arguments.input, result, samples
        )
        print(
            f"[parity] ONNX vs KADD-CNN max diff = {maximum_error:.2e} "
            f"({arguments.verify_samples} samples)"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

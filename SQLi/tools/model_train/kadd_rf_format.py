"""KADD-RF v1 portable model exporter and reference reader.

The format is fixed-width little-endian. It is intended to be produced on an
x86 training host and consumed by the KADD C runtime on x86-64 or AArch64/SVE.
"""

from __future__ import annotations

import os
import struct
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np


MAGIC = b"KADDRF1\0"
VERSION = 1
HEADER = struct.Struct("<8sIIIIIIIIIQIII")
NODE = struct.Struct("<iIIIdd")
LEAF_FEATURE = -1
NO_CHILD = 0xFFFFFFFF

DATASET_MAGIC = b"KADDS1\0\0"
DATASET_VERSION = 1
DATASET_HEADER = struct.Struct("<8sIIIIII")


def _validate_classifier(model) -> None:
    if not hasattr(model, "estimators_") or not hasattr(model, "classes_"):
        raise TypeError("model must be a fitted sklearn RandomForestClassifier")
    classes = np.asarray(model.classes_)
    if classes.shape != (2,) or not np.array_equal(classes, np.array([0, 1])):
        raise ValueError(
            f"KADD-RF requires binary classes [0, 1], got {classes.tolist()}"
        )
    if int(model.n_features_in_) <= 0:
        raise ValueError("invalid sklearn feature count")


def export_kadd_rf(model, output_path: os.PathLike[str] | str) -> Path:
    """Export a fitted sklearn RandomForestClassifier to KADD-RF v1."""
    _validate_classifier(model)
    output = Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)

    tree_offsets = [0]
    node_blobs: list[bytes] = []

    for estimator in model.estimators_:
        tree = estimator.tree_
        base = tree_offsets[-1]
        for local_index in range(tree.node_count):
            feature = int(tree.feature[local_index])
            if feature < 0:
                counts = np.asarray(tree.value[local_index], dtype=np.float64).reshape(-1)
                if counts.size != 2:
                    raise ValueError(
                        f"tree leaf has {counts.size} classes, expected 2"
                    )
                total = float(counts.sum())
                if not np.isfinite(total) or total <= 0.0:
                    raise ValueError("tree leaf has invalid class weight sum")
                positive_probability = float(counts[1] / total)
                node_blobs.append(
                    NODE.pack(
                        LEAF_FEATURE,
                        NO_CHILD,
                        NO_CHILD,
                        0,
                        0.0,
                        positive_probability,
                    )
                )
                continue

            left = int(tree.children_left[local_index])
            right = int(tree.children_right[local_index])
            threshold = float(tree.threshold[local_index])
            if feature >= int(model.n_features_in_):
                raise ValueError("tree contains an out-of-range feature index")
            if left <= local_index or right <= local_index:
                raise ValueError("KADD-RF requires forward child indices")
            if not np.isfinite(threshold):
                raise ValueError("tree contains a non-finite threshold")
            node_blobs.append(
                NODE.pack(
                    feature,
                    base + left,
                    base + right,
                    0,
                    threshold,
                    0.0,
                )
            )
        tree_offsets.append(base + int(tree.node_count))

    offsets_blob = struct.pack(
        f"<{len(tree_offsets)}I", *tree_offsets
    )
    payload = offsets_blob + b"".join(node_blobs)
    checksum = zlib.crc32(payload) & 0xFFFFFFFF
    header = HEADER.pack(
        MAGIC,
        VERSION,
        HEADER.size,
        NODE.size,
        int(model.n_features_in_),
        2,
        1,
        len(model.estimators_),
        tree_offsets[-1],
        0,
        len(payload),
        checksum,
        0,
        0,
    )

    fd, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", suffix=".tmp", dir=output.parent
    )
    try:
        with os.fdopen(fd, "wb") as stream:
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
class PortableNode:
    feature: int
    left: int
    right: int
    threshold: float
    positive_probability: float


@dataclass(frozen=True)
class PortableForest:
    feature_count: int
    tree_offsets: tuple[int, ...]
    nodes: tuple[PortableNode, ...]

    def predict_proba(self, features: np.ndarray) -> np.ndarray:
        matrix = np.asarray(features, dtype=np.float32)
        if matrix.ndim != 2 or matrix.shape[1] != self.feature_count:
            raise ValueError(
                f"expected shape (n, {self.feature_count}), got {matrix.shape}"
            )
        result = np.zeros((matrix.shape[0], 2), dtype=np.float64)
        for row_index, row in enumerate(matrix):
            positive_sum = 0.0
            for root in self.tree_offsets[:-1]:
                node_index = root
                while self.nodes[node_index].feature >= 0:
                    node = self.nodes[node_index]
                    node_index = (
                        node.left
                        if float(row[node.feature]) <= node.threshold
                        else node.right
                    )
                positive_sum += self.nodes[node_index].positive_probability
            positive = positive_sum / (len(self.tree_offsets) - 1)
            result[row_index] = (1.0 - positive, positive)
        return result


def load_kadd_rf(path: os.PathLike[str] | str) -> PortableForest:
    """Reference parser used by exporter parity tests."""
    data = Path(path).read_bytes()
    if len(data) < HEADER.size:
        raise ValueError("truncated KADD-RF model")
    unpacked = HEADER.unpack_from(data)
    (
        magic,
        version,
        header_size,
        node_size,
        feature_count,
        class_count,
        positive_index,
        tree_count,
        node_count,
        _flags,
        payload_size,
        checksum,
        _reserved0,
        _reserved1,
    ) = unpacked
    if (
        magic != MAGIC
        or version != VERSION
        or header_size != HEADER.size
        or node_size != NODE.size
        or class_count != 2
        or positive_index != 1
    ):
        raise ValueError("unsupported KADD-RF header")
    payload = data[HEADER.size :]
    if len(payload) != payload_size or zlib.crc32(payload) & 0xFFFFFFFF != checksum:
        raise ValueError("invalid KADD-RF payload")

    offsets_size = (tree_count + 1) * 4
    offsets = struct.unpack_from(f"<{tree_count + 1}I", payload)
    if offsets[0] != 0 or offsets[-1] != node_count:
        raise ValueError("invalid tree offsets")
    nodes: list[PortableNode] = []
    cursor = offsets_size
    for _ in range(node_count):
        feature, left, right, _reserved, threshold, probability = NODE.unpack_from(
            payload, cursor
        )
        nodes.append(
            PortableNode(feature, left, right, threshold, probability)
        )
        cursor += NODE.size
    return PortableForest(feature_count, tuple(offsets), tuple(nodes))


def assert_export_parity(
    model,
    model_path: os.PathLike[str] | str,
    samples: np.ndarray,
    *,
    atol: float = 1e-12,
) -> float:
    """Raise on semantic drift and return the maximum probability error."""
    portable = load_kadd_rf(model_path)
    sklearn_probability = model.predict_proba(
        np.asarray(samples, dtype=np.float32)
    )
    portable_probability = portable.predict_proba(samples)
    maximum_error = float(
        np.max(np.abs(sklearn_probability - portable_probability), initial=0.0)
    )
    if maximum_error > atol:
        raise AssertionError(
            f"sklearn/KADD-RF parity failed: max error={maximum_error:.3e}"
        )
    return maximum_error


def export_kadd_dataset(
    features: np.ndarray,
    labels: np.ndarray,
    output_path: os.PathLike[str] | str,
) -> Path:
    """Write a labeled float32 matrix for the native C accuracy benchmark.

    Payload layout is row-major little-endian float32 features followed by one
    uint8 label per row. A CRC32 protects the complete payload.
    """
    matrix = np.asarray(features, dtype="<f4")
    target = np.asarray(labels)
    if matrix.ndim != 2 or matrix.shape[0] == 0 or matrix.shape[1] == 0:
        raise ValueError("features must be a non-empty 2-D matrix")
    if target.ndim != 1 or target.shape[0] != matrix.shape[0]:
        raise ValueError("labels must have one value per feature row")
    if not np.all((target == 0) | (target == 1)):
        raise ValueError("KADD benchmark labels must be binary (0 or 1)")
    if not np.all(np.isfinite(matrix)):
        raise ValueError("features contain NaN or infinity")

    output = Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)
    feature_blob = np.ascontiguousarray(matrix).tobytes(order="C")
    label_blob = np.ascontiguousarray(target, dtype=np.uint8).tobytes(order="C")
    payload = feature_blob + label_blob
    header = DATASET_HEADER.pack(
        DATASET_MAGIC,
        DATASET_VERSION,
        DATASET_HEADER.size,
        int(matrix.shape[0]),
        int(matrix.shape[1]),
        zlib.crc32(payload) & 0xFFFFFFFF,
        0,
    )

    fd, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", suffix=".tmp", dir=output.parent
    )
    try:
        with os.fdopen(fd, "wb") as stream:
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

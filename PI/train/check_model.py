# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# All rights reserved.
#
# Licensed under the 3-Clause BSD License (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://opensource.org/license/bsd-3-clause
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import argparse
import math
from collections import defaultdict

import onnx
from onnx import TensorProto


DTYPE_ALIAS = {
    TensorProto.FLOAT: "float32",
    TensorProto.FLOAT16: "float16",
    TensorProto.BFLOAT16: "bfloat16",
    TensorProto.DOUBLE: "float64",
    TensorProto.INT8: "int8",
    TensorProto.UINT8: "uint8",
    TensorProto.INT16: "int16",
    TensorProto.UINT16: "uint16",
    TensorProto.INT32: "int32",
    TensorProto.UINT32: "uint32",
    TensorProto.INT64: "int64",
    TensorProto.UINT64: "uint64",
    TensorProto.BOOL: "bool",
}


def numel(dims):
    if not dims:
        return 1
    return math.prod(dims)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("model", help="ONNX model path")
    args = parser.parse_args()

    model = onnx.load(args.model, load_external_data=True)

    type_counts = defaultdict(int)
    total = 0

    for tensor in model.graph.initializer:
        dtype = DTYPE_ALIAS.get(
            tensor.data_type,
            TensorProto.DataType.Name(tensor.data_type).lower(),
        )
        count = numel(tensor.dims)

        type_counts[dtype] += count
        total += count

    if total == 0:
        return

    for dtype, count in sorted(
        type_counts.items(),
        key=lambda x: x[1],
        reverse=True,
    ):
        ratio = count / total * 100
        print(f"{dtype}: {ratio:.2f}%")


if __name__ == "__main__":
    main()

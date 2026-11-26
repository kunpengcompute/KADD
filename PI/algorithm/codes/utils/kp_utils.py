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
import os
import time
import json
from pathlib import Path
from typing import Union
from functools import wraps

import numpy as np


def time_stat(func):
    @wraps(func)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = func(*args, **kwargs)
        duration = time.perf_counter() - start
        return result, duration
    return wrapper


def load_json(file_path: Union[str, Path]):
    path = Path(file_path)

    if not path.exists():
        raise FileNotFoundError("Load Json Failed: File not exists.")

    if not path.is_file() or path.suffix != ".json":
        raise ValueError("Load Json Failed: File is not a JSON File.")

    if not os.access(file_path, os.R_OK):
        raise PermissionError("Load Json Failed: File is not accessible.")

    with open(file_path, 'r', encoding="utf-8") as f:
        try:
            return json.load(f)
        except json.JSONDecodeError:
            raise ValueError("Load Json Failed: File is not JSON formatted.")

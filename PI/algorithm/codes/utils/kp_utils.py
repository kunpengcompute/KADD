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


def softmax(logits: np.ndarray, axis: int = -1) -> np.ndarray:
    logits = logits - np.max(logits, axis=axis, keepdims=True)  # 数值稳定
    exp_logits = np.exp(logits)
    return exp_logits / np.sum(exp_logits, axis=axis, keepdims=True)

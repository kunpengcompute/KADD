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
import time
from typing import Dict, List
from logging import Logger

from algorithm.codes.kp_config import KunpengConstant
from algorithm.codes.kp_jailbreak_detection import JailbreakDetection
from algorithm.codes.kp_exception import (KpInitializationException, KpDetectionException,
                                          KpUpdateException, KpErrorCode)


def kp_detector_initialization(logger: Logger):
    try:
        return JailbreakDetection(logger)
    except KpInitializationException:
        raise
    except Exception as e:
        raise KpInitializationException(
            KpErrorCode.InitError, 
            str(e)
        ) from e

def kp_detect(kp_detector: JailbreakDetection, prompt: str):
    if not isinstance(kp_detector, JailbreakDetection):
        raise KpDetectionException(
            KpErrorCode.DetectError,
            "invalid kp_detector type."
        )
    
    start = time.perf_counter()

    try:
        result = kp_detector.detect(prompt)
    except KpDetectionException:
        raise 
    except Exception as e:
        raise KpDetectionException(
            KpErrorCode.DetectError,
            str(e)
        ) from e
    
    code = KunpengConstant.JAILBREAK if result else KunpengConstant.BENIGN
    duration = f"{(time.perf_counter() - start) * 1000:.3f} ms"
    
    return {
        "result": code,
        "detect_time": duration
    }


def kp_detector_update_blackwhitelist(kp_detector: JailbreakDetection, blackwhitelist: Dict[str, List[str]]):
    if not isinstance(kp_detector, JailbreakDetection):
        raise KpUpdateException(
            KpErrorCode.UpdateError,
            "invalid kp_detector type."
        )
    try:
        kp_detector.update_blackwhitelist(blackwhitelist)
    except KpUpdateException:
        raise
    except Exception as e:
        raise KpUpdateException(
            KpErrorCode.UpdateError,
            str(e)
        ) from e
    
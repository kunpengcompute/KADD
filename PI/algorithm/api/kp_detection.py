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
    try:
        kp_detector.update_blackwhitelist(blackwhitelist)
    except KpUpdateException:
        raise
    except Exception as e:
        raise KpUpdateException(
            KpErrorCode.UpdateError,
            str(e)
        ) from e
    
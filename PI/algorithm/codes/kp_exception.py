from enum import IntEnum

from algorithm.codes.kp_config import KunpengConstant


class KpExceptionBase(Exception):
    def __init__(self, error_code, msg):
        self.error_code = error_code
        self.msg = msg
        super().__init__(msg)

    def __str__(self):
        return f"{KunpengConstant.LOG_PREFIX}ERROR: {self.error_code} - {self.msg}"


class KpInitializationException(KpExceptionBase):
    pass


class KpDetectionException(KpExceptionBase):
    pass


class KpUpdateException(KpExceptionBase):
    pass


class KpErrorCode(IntEnum):
    InitError = 10000
    DetectError = 20000
    UpdateError = 30000

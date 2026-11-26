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

    def __str__(self):
        return str(self.value)

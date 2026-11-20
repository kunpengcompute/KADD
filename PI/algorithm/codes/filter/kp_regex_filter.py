# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# All rights reserved.
#
# Licensed under the License;
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
from logging import Logger

from algorithm.codes.utils.kp_utils import time_stat


class RegexFilter:
    def __init__(self, config, logger: Logger):
        self.logger = logger

        self.naive_regex_patterns = config.naive_regex

    def naive_regex_match(self, prompt):
        for pattern in self.naive_regex_patterns:
            if pattern.search(prompt):
                return True
        return False

    @time_stat
    def run_filter(self, prompt):
        try:
            return self.naive_regex_match(prompt)
        except Exception:
            raise
        
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
import os
import logging
from pathlib import Path
from datetime import datetime

from trainer import Trainer
from utils.utils import get_logger
from quant import quant

if __name__ == "__main__":
    base_dir = Path(__file__).resolve().parent
    log_dir = os.path.realpath(os.path.join(base_dir, "log"))
    os.makedirs(log_dir, exist_ok=True)

    log_file = os.path.realpath(os.path.join(base_dir, "log", f"log_{datetime.now().strftime(r'%Y-%m-%d_%H-%M-%S')}.log"))
    logger = get_logger(log_file=log_file, level=logging.INFO)
    trainer = Trainer(logger)

    trainer.train()
    quant()

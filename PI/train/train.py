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

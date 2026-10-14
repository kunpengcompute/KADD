from algorithm.api.kp_detection import kp_detect, kp_detector_initialization, kp_detector_update_blackwhitelist
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path


def get_logger(
    name: str = "",
    log_file = None,
    level: int = logging.INFO,
    max_bytes: int = 10 * 1024 * 1024,
    backup_count: int = 5,
) -> logging.Logger:
    logger = logging.getLogger(name)
    logger.setLevel(level)
    logger.propagate = False

    if logger.handlers:
        return logger

    formatter = logging.Formatter(
        fmt="%(asctime)s [%(levelname)s] %(name)s - %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    console_handler = logging.StreamHandler()
    console_handler.setLevel(level)
    console_handler.setFormatter(formatter)
    logger.addHandler(console_handler)

    if log_file:
        log_path = Path(log_file)
        log_path.parent.mkdir(parents=True, exist_ok=True)

        file_handler = RotatingFileHandler(
            filename=log_file,
            maxBytes=max_bytes,
            backupCount=backup_count,
            encoding="utf-8",
        )
        file_handler.setLevel(level)
        file_handler.setFormatter(formatter)
        logger.addHandler(file_handler)

    return logger


if __name__ == "__main__":
    """
    初始化实例
    """
    logger = get_logger(level=logging.INFO)
    kp = kp_detector_initialization(logger)

    while True:
        prompt = input("请输入检测文本（输入q退出）：\n").strip()
        if prompt.lower() == "q":
            break
        if not prompt:
            continue

        # 检测
        res = kp_detect(kp, prompt)
        print(res)

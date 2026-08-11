import os
import re
from pathlib import Path


_BASE_DIR = Path(__file__).resolve().parent.parent

class KunpengConstant:
    LOG_PREFIX = "[Kunpeng Detection] "
    JAILBREAK = 1
    BENIGN = 0
    KNOWLEDGE_BASE_PATH = os.path.realpath(os.path.join(_BASE_DIR, "knowledge_base"))
    ALLOWED_CONFIG_KEYS = ["blacklist", "whitelist"]
    ALLOWED_CONFIG_TYPES = {
        "blacklist": list,
        "whitelist": list
    }
    ALLOWED_CONFIG_SIZE = {
        "blacklist": 500,
        "whitelist": 500
    }


class BlackwhitelistConfig:
    seed = 42
    schema = "affine32"
    num_perm = 128
    threshold = 0.8
    blackwhitelist_path = os.path.join(KunpengConstant.KNOWLEDGE_BASE_PATH, "blackwhitelist", "blackwhitelist.json")
    jieba_tmp_dir = os.path.join(KunpengConstant.KNOWLEDGE_BASE_PATH, "blackwhitelist")
    jieba_init_sentence = "初始化文本"


class RegexConfig:
    naive_regex: list[re.Pattern[str]] = [
        re.compile(r"请以.{0,50}开始回答"),
        re.compile(r"继续.{0,50}对话"),
        re.compile(r"不可以.{0,50}拒绝"),
        re.compile(r"(越狱|root|不受限).{0,10}模式", re.IGNORECASE),
        re.compile(r"系统prompt", re.IGNORECASE)
    ]


class ClassifierConfig:
    device = "cpu"
    onnx_path = os.path.join(KunpengConstant.KNOWLEDGE_BASE_PATH, "model_file", "clf_model.onnx")
    model_path = os.path.join(KunpengConstant.KNOWLEDGE_BASE_PATH, "model_file") 
    confidence_threshold = 0.5
    intra_op_num_threads = 16
    inter_op_num_threads = 16
    log_severity_level = 3

import math
from typing import Dict
from logging import Logger

from transformers import AutoTokenizer
import onnxruntime as ort

from algorithm.codes.utils.kp_utils import time_stat


class ClassifierFilter:
    def __init__(self, config: Dict, logger: Logger):
        self.logger = logger

        self.device = config.device
        self.onnx_path = config.onnx_path
        self.model_path = config.model_path
        self.confidence_threshold = config.confidence_threshold
        self.logit_threshold = math.log(self.confidence_threshold / (1 - self.confidence_threshold))

        self.intra_op_num_threads = config.intra_op_num_threads
        self.inter_op_num_threads = config.inter_op_num_threads
        self.log_severity_level = config.log_severity_level

        self.tokenizer = self.load_tokenizer()
        self.ort_session = self.load_session()

        self.warmup()

    def warmup(self):
        dummy_input = self.tokenizer(
            "warmup",
            truncation=True,
            max_length=512,
            padding=False,
            return_tensors="np",
        )
        self.ort_session.run(
            ["logits"],
            {
                "input_ids": dummy_input["input_ids"],
                "attention_mask": dummy_input["attention_mask"],
            }
        )

    def load_tokenizer(self):
        try:
            tokenizer = AutoTokenizer.from_pretrained(
                self.model_path, 
                use_fast=True
            )
            return tokenizer
        except Exception:
            raise

    def load_session(self):
        try:
            session_options = ort.SessionOptions()

            session_options.intra_op_num_threads = self.intra_op_num_threads
            session_options.inter_op_num_threads = self.inter_op_num_threads
            session_options.log_severity_level = self.log_severity_level
            session_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
            session_options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL

            session_options.enable_mem_pattern = True
            session_options.enable_cpu_mem_arena = True

            session = ort.InferenceSession(self.onnx_path, sess_options=session_options)
            return session
        except Exception:
            raise

    @time_stat
    def run_filter(self, prompt: str):
        inputs = self.tokenizer(
            prompt,
            truncation=True,
            max_length=512,
            padding=False,
            return_tensors="np",
            add_special_tokens=True,
        )

        logits = self.ort_session.run(
            ["logits"],
            {
                "input_ids": inputs["input_ids"],
                "attention_mask": inputs["attention_mask"],
            }
        )[0]

        return (logits[0, 1] - logits[0, 0]) > self.logit_threshold

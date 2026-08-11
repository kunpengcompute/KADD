import os
import torch
import sys
import subprocess
from pathlib import Path

from onnxruntime.quantization import quantize_dynamic, QuantType

from utils.utils import load_model

def quant():
    _base_dir = Path(__file__).resolve().parent
    pretrained_model = os.path.realpath(os.path.join(_base_dir, "pretrained_model", "chinese-lert-small"))
    finetuned_model = os.path.realpath(os.path.join(_base_dir, "finetuned_model", "Chinese-lert-small_cross_entropy_best.bin"))

    _, model = load_model(pretrained_model, finetuned_model, 2, "cpu")

    onnx_model = os.path.realpath(os.path.join(_base_dir, "onnx_model", "clf_model_fp32.onnx"))

    SEQ_LEN = 128

    torch.onnx.export(
        model.cpu().eval(),
        (torch.ones(1, SEQ_LEN).long(), torch.ones(1, SEQ_LEN).long()),
        onnx_model,
        input_names=["input_ids", "attention_mask"],
        output_names=["pooled_output", "logits"],
        dynamic_axes={
            "input_ids": {0: "batch_size", 1: "sequence_length"},
            "attention_mask": {0: "batch_size", 1: "sequence_length"},
            "pooled_output": {0: "batch_size"},
            "logits": {0: "batch_size"}
        },
        opset_version=18
    )

    onnx_inter_model = os.path.realpath(os.path.join(_base_dir, "onnx_model", "clf_model_fp32_inter.onnx"))
    subprocess.run([
        sys.executable, "-m", "onnxruntime.quantization.preprocess",
        "--input", onnx_model,
        "--output", onnx_inter_model,
        "--skip_symbolic_shape", "True"
    ], check=True)
    
    onnx_int8_model = os.path.realpath(os.path.join(_base_dir, "onnx_model", "clf_model.onnx"))
    quantize_dynamic(
        onnx_inter_model,
        onnx_int8_model,
        per_channel=True,
        reduce_range=True,
        weight_type=QuantType.QInt8
    )

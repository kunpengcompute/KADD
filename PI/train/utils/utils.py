import os
import torch
from torch.nn.utils.rnn import pad_sequence

import json
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
from typing import Union, Dict
from transformers import AutoTokenizer

from model.backbone import Backbone
from model.classifier import Classifier


def get_logger(
    name: str = "kunpeng_model_finetuning",
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


def check_file_valid(file_path: Union[str, Path], prefix=""):
    path = Path(file_path)
    
    if not path.exists():
        raise FileNotFoundError(f"Load File Failed: {file_path} not exists.")

    if not path.is_file():
        raise ValueError(f"Load File Failed: {file_path} is not a file.")

    if prefix and  path.suffix != prefix:
        raise ValueError(f"Load File Failed: {file_path} is not a {prefix} File.")

    if not os.access(file_path, os.R_OK):
        raise PermissionError(f"Load File Failed: {file_path} is not accessible.")


def load_json(file_path: Union[str, Path]):
    check_file_valid(file_path, ".json")

    with open(file_path, 'r', encoding="utf-8") as f:
        try:
            return json.load(f)
        except json.JSONDecodeError:
            raise ValueError("Load Json Failed: File is not JSON formatted.")


def get_dataset(file_path: Union[str, Path], label2id: Dict[str, int]):
    data = load_json(file_path)

    texts, labels = [], []
    try:
        for k, v in data.items():
            texts.extend(v)
            labels.extend([label2id[k]] * len(v))

        return texts, labels
    except Exception:
        raise ValueError(f"Load Dataset Failed: {file_path} must be Dict[label: List[text]] formatted.")


def data_collate_fn(batch, tokenizer):
    input_ids = [torch.tensor(item["input_ids"]) for item in batch]
    attention_mask = [torch.tensor(item["attention_mask"]) for item in batch]
    labels = torch.tensor([item["label"] for item in batch])
    texts = [item["text"] for item in batch]

    input_ids_padded = pad_sequence(input_ids, batch_first=True, padding_value=tokenizer.pad_token_id)
    attention_mask_padded = pad_sequence(attention_mask, batch_first=True, padding_value=0)

    return {
        "input_ids": input_ids_padded,
        "attention_mask": attention_mask_padded,
        "labels": labels,
        "texts": texts
    }


def load_model(pretrained_model, finetuned_model, num_classes, device):
    tokenizer = AutoTokenizer.from_pretrained(pretrained_model)
    model = Classifier(Backbone(pretrained_model), num_classes)

    checkpoint = torch.load(finetuned_model, map_location=device)
    model.load_state_dict(checkpoint["model_state_dict"])

    model.to(device)
    model.eval()

    return tokenizer, model


def convert_model_to_onnx(pretrained_model, finetuned_model, num_classes, onnx_path):
    device = "cuda:0" if torch.cuda.is_available() else "cpu"

    tokenizer, model = load_model(pretrained_model, finetuned_model, num_classes, device)

    inputs = tokenizer(
        "This is a dummy text",
        truncation=True,
        max_length=512,
        padding="max_length",
        return_tensors="pt"
    ).to(device)

    with torch.no_grad():
        torch.onnx.export(
            model=model,
            args=[inputs["input_ids"], inputs["attention_mask"]],
            f=onnx_path,
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

import os
import torch
from torch.utils.data import DataLoader

from tqdm import tqdm
from pathlib import Path
from logging import Logger
from functools import partial
from sklearn.model_selection import train_test_split
from transformers import AutoTokenizer, get_cosine_schedule_with_warmup

from dataset.dataset import TextClassificationDataset
from model.backbone import Backbone
from model.classifier import Classifier
from tester import Tester
from loss_fn.loss_func import FocalLoss

from utils.utils import load_json, get_dataset, data_collate_fn, convert_model_to_onnx

class Trainer:
    _BASE_DIR = Path(__file__).resolve().parent
    def __init__(self, logger: Logger, config_path=f"config/config.json"):
        self.logger = logger
        self.device = "cuda:0" if torch.cuda.is_available() else "cpu"
        self.config = load_json(os.path.realpath(os.path.join(self._BASE_DIR, config_path)))

    @staticmethod
    def get_optimizer_and_scheduler(model, train_loader, num_epochs, learning_rate, weight_decay, warmup_rate):
        total_steps = len(train_loader) * num_epochs
        warmup_steps = int(total_steps * warmup_rate)

        optimizer = torch.optim.Adam(
            model.parameters(),
            lr=learning_rate,
            weight_decay=weight_decay
        )

        scheduler = get_cosine_schedule_with_warmup(
            optimizer=optimizer,
            num_warmup_steps=warmup_steps,
            num_training_steps=total_steps
        )

        return optimizer, scheduler        

    def get_train_data(self, tokenizer):
        dataset_path = os.path.realpath(os.path.join(self._BASE_DIR, "dataset", self.config.get("dataset")))
        texts, labels = get_dataset(dataset_path, self.config.get("label2id"))

        train_texts, val_texts, train_labels, val_labels = train_test_split(
            texts,
            labels,
            test_size=self.config.get("val_ratio", 0.1),
            stratify=labels
        )

        train_dataset = TextClassificationDataset(
            train_texts,
            train_labels,
            tokenizer,
            self.config.get("max_length", 512)
        )

        val_dataset = TextClassificationDataset(
            val_texts,
            val_labels,
            tokenizer,
            self.config.get("max_length", 512)
        )

        collate_fn = partial(data_collate_fn, tokenizer=tokenizer)
        train_dataloader = DataLoader(
            train_dataset,
            batch_size=self.config.get("batch_size"),
            collate_fn=collate_fn,
            shuffle=True,
            drop_last=False
        )

        val_dataloader = DataLoader(
            val_dataset,
            batch_size=self.config.get("batch_size"),
            collate_fn=collate_fn,
            shuffle=False,
            drop_last=False
        )

        return train_dataset, train_dataloader, val_dataset, val_dataloader

    def get_all_data_to_trainset(self, tokenizer):
        dataset_path = os.path.realpath(os.path.join(self._BASE_DIR, "dataset", self.config.get("dataset")))
        texts, labels = get_dataset(dataset_path, self.config.get("label2id"))

        train_dataset = TextClassificationDataset(
            texts,
            labels,
            tokenizer,
            self.config.get("max_length", 512)
        )

        collate_fn = partial(data_collate_fn, tokenizer=tokenizer)
        train_dataloader = DataLoader(
            train_dataset,
            batch_size=self.config.get("batch_size"),
            collate_fn=collate_fn,
            shuffle=True,
            drop_last=False
        )

        return train_dataset, train_dataloader

    @staticmethod
    def train_one_epoch(model, dataloader, optimizer, scheduler, loss_fn, device):
        model.train()
        total_loss = 0

        progress_bar = tqdm(dataloader, desc=f"Training", leave=False)

        for batch in progress_bar:
            input_ids = batch["input_ids"].to(device)
            attention_mask = batch["input_ids"].to(device)
            labels = batch["labels"].to(device)

            optimizer.zero_grad()

            pooled_output, logits = model(input_ids, attention_mask)

            loss = loss_fn(logits, labels)
            loss.backward()

            torch.nn.utils.clip_grad_norm_(model.parameters(), max_norm=1.0)

            optimizer.step()
            scheduler.step()

            total_loss += loss.item()
            progress_bar.set_postfix({"loss": f"{loss.item():.4f}"})

        return total_loss / len(dataloader)

    def train(self):
        model_name = self.config.get("pretrained_model")
        model_path = os.path.realpath(os.path.join(self._BASE_DIR, "pretrained_model" ,model_name))
        tokenizer = AutoTokenizer.from_pretrained(model_path)

        backbone = Backbone(model_path)
        classifier = Classifier(backbone, self.config.get("num_classes"), self.config.get("dropout"))
        classifier.to(self.device)

        tester = Tester(self.logger)

        train_dataset, train_dataloader = self.get_all_data_to_trainset(tokenizer)

        self.logger.info(f"Number of training dataset: {len(train_dataset)}")

        if self.config.get("loss_fn") == "focalloss":
            loss_fn = FocalLoss(1, 2)
        elif self.config.get("loss_fn") == "cross_entropy":
            loss_fn = FocalLoss(1, 0)

        epochs = self.config.get("epochs")

        optimizer, scheduler = self.get_optimizer_and_scheduler(
            classifier, 
            train_dataloader, 
            num_epochs=epochs,
            learning_rate=self.config.get("lr"),
            weight_decay=self.config.get("weight_decay"),
            warmup_rate=self.config.get("warmup_rate")
        )

        best_epoch = 0
        best_acc = -1
        self.logger.info("Start Training!")
        for epoch in range(epochs):
            self.logger.info(f"Training on Epoch {epoch + 1}/{epochs}")

            avg_loss = self.train_one_epoch(
                classifier,
                train_dataloader,
                optimizer,
                scheduler,
                loss_fn,
                self.device
            )

            acc = tester.test(
                classifier,
                train_dataloader,
                loss_fn,
                self.device
            )

            if self.config.get("save_epoch") == "best":
                if acc > best_acc:
                    self.save_model(classifier, epoch + 1)
                    best_acc = acc
                    best_epoch = epoch + 1

                    self.logger.info(f"New best Model saved: best epoch {best_epoch}; best acc {acc * 100:.4f}%.")

    def save_model(self, model, epoch, mode="best"):
        model_name = self.config.get("pretrained_model")
        basedir = os.path.realpath(os.path.join(self._BASE_DIR, self.config.get("save_path")))
        os.makedirs(basedir, exist_ok=True)
        loss_fn = self.config.get("loss_fn")

        if mode== "best":
            save_path = os.path.realpath(os.path.join(basedir, f"{model_name}_{loss_fn}_best.bin"))
        else:
            save_path = os.path.realpath(os.path.join(basedir, f"{model_name}_{loss_fn}_epoch_{epoch}.bin"))

        try:
            torch.save({"model_state_dict": model.state_dict()}, save_path)

            self.logger.info(f"Model save success: save to {save_path.split(os.path.sep)[-1]}.")
        except Exception as e:
            self.logger.info(f"Model save failed: {e}")

    def convert_model_to_onnx(self, finetuned_model):
        model_name = self.config.get("pretrained_model")
        pretrained_model = os.path.realpath(os.path.join(self._BASE_DIR, "pretrained_model" , model_name))
        num_classes = self.config.get("num_classes")
        onnx_model_name = self.config.get("onnx_model")
        onnx_dir = os.path.realpath(os.path.join(self._BASE_DIR, self.config.get("onnx_path")))
        os.makedirs(onnx_dir, exist_ok=True)
        onnx_path = os.path.realpath(os.path.join(onnx_dir, f"{onnx_model_name}"))

        try:
            convert_model_to_onnx(pretrained_model, finetuned_model, num_classes, onnx_path)
            self.logger.info(f"Model Convert Success: Save to {onnx_path}")
        except Exception as e:
            self.logger.error(f"Model Convert Failed: {e}")     
    
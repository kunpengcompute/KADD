import torch
import torch.nn.functional as F
from sklearn.metrics import accuracy_score, confusion_matrix

from tqdm import tqdm


class Tester:
    def __init__(self, logger):
        self.logger = logger

    def test(self, model, dataloader, loss_fn, device, confidence_threshold=0.5):
        total_loss = 0
        gt_labels = []
        predictions = []

        progress_bar = tqdm(dataloader, desc=f"Testing", leave=False)

        model.eval()
        with torch.no_grad():
            for batch in progress_bar:
                input_ids = batch["input_ids"].to(device)
                attention_mask = batch["input_ids"].to(device)
                labels = batch["labels"].to(device)

                pooled_output, logits = model(input_ids, attention_mask)

                loss = loss_fn(logits, labels)
                total_loss += loss.item()

                probs = F.softmax(logits, dim=-1)
                pos_probs = probs[:, 1]
                preds = (pos_probs.cpu().numpy() > confidence_threshold).astype(int).tolist()

                gt_labels.extend(labels.cpu().tolist())
                predictions.extend(preds)

        avg_loss = total_loss / len(dataloader)
        acc = accuracy_score(gt_labels, predictions)

        tn, fp, fn, tp = confusion_matrix(gt_labels, predictions, labels=[0, 1]).ravel()
        fnr = fn / (tp + fn) if tp + fn != 0 else 0
        fpr = fp / (tn + fp) if tn + fp != 0 else 0

        self.logger.info(f"AVG_LOSS:{avg_loss: .4f}; 漏报率:{fnr * 100: 0.4f}%; 误报率:{fpr * 100: 0.4f}%")

        return acc

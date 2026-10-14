import torch
import torch.nn as nn
from transformers import AutoModel

class Backbone(nn.Module):
    def __init__(self, pretrained_model):
        super().__init__()
        try:
            self.backbone = AutoModel.from_pretrained(pretrained_model)
        except:
            raise ValueError("Backbone Model failed: pretrained model type mismatched.")

    def forward(self, input_ids, attention_mask, token_type_ids=None):
        outputs = self.backbone(
            input_ids = input_ids,
            attention_mask = attention_mask,
            token_type_ids = token_type_ids,
            output_hidden_states = False,
            output_attentions = False
        )

        sequence_outputs = outputs.last_hidden_state
        pooled_output = sequence_outputs[:, 0, :]

        return pooled_output
    
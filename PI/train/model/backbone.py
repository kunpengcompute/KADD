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
    
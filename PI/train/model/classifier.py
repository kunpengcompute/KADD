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

from .backbone import Backbone

class Classifier(nn.Module):
    def __init__(self, backbone, num_classes, dropout = 0.1):
        super().__init__()

        self.backbone = backbone
        if isinstance(self.backbone, Backbone):
            hidden_size = self.backbone.backbone.config.hidden_size
        else:
            raise ValueError("Classfier model failed: backbone mismatched.")

        self.dropout = nn.Dropout(dropout)
        self.classifier = nn.Linear(hidden_size, num_classes)

    def forward(self, input_ids, attention_mask, token_type_ids=None):
        pooled_output = self.backbone(
            input_ids,
            attention_mask,
            token_type_ids
        )
        logits = self.classifier(self.dropout(pooled_output))
    
        return pooled_output, logits
    
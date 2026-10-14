"""SQLi 检测模型统一训练脚本 — 支持 RF/DL_CNN 两种模型

【模型类型】
- RF:     随机森林，输入 28 维特征向量（seq_to_feature），同时输出
          model_tokens_rf.onnx 和跨平台 model_tokens_rf.krf
- DL_CNN: TextCNN，输入 token 序列（300 维 pad），同时输出 ONNX、
          跨平台 KADD-CNN 模型和独立测试集

【特征生成流程】
Raw CSV (Query, Label)
    ↓ ctypes 调用 C 动态库
DFA tokenize (libkadd_dfa_engine.dll/so) → token 序列
    ↓
┌─ RF: seq_to_feature (libkadd_feature_engine.dll/so) → 28 维特征向量
│   滑动窗口算法 (window_size=5)，特征值范围 0~9789
│
└─ DL_CNN: token pad → 300 维 token 序列 (float)

【依赖】
pip install torch onnx "onnxruntime>=1.28.0" pandas numpy scikit-learn skl2onnx

【使用】
python model_train.py --model-type rf        # 随机森林
python model_train.py --model-type dl_cnn    # TextCNN
"""
import os
import sys
import ctypes
import json
import argparse
import numpy as np
import pandas as pd
from collections import Counter
import platform

import torch
import torch.nn as nn
import onnx
import onnxruntime as ort
from sklearn.model_selection import train_test_split
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix, f1_score, roc_auc_score
from sklearn.ensemble import RandomForestClassifier
from kadd_rf_format import (
    export_kadd_rf,
    assert_export_parity as assert_rf_export_parity,
    export_kadd_dataset,
)
from kadd_cnn_format import (
    export_kadd_cnn,
    assert_export_parity as assert_cnn_export_parity,
)


# ==================== 配置参数 ====================
class Config:
    """全局配置类：包含路径、模型参数、训练参数"""

    # --- 路径配置 ---
    SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
    # C 动态库路径（Windows 测试用 dll，Linux 部署用 so）
    DFA_SO = os.path.join(SCRIPT_DIR, "libkadd_dfa_engine.dll")
    FEATURE_SO = os.path.join(SCRIPT_DIR, "libkadd_feature_engine.dll")
    # 配置文件（包含 DFA 三表路径）
    CONFIG_JSON = os.path.join(SCRIPT_DIR, "../../config.json")
    # 训练/验证数据 CSV（必须包含 Query 和 Label 列）
    # train_data.csv: 80% 训练集
    # verify_data.csv: 20% 验证集，训练阶段绝不参与拟合
    RAW_CSV = os.path.join(SCRIPT_DIR, "train_data", "train_data.csv")
    VERIFY_CSV = os.path.join(SCRIPT_DIR, "train_data", "verify_data.csv")
    FULL_CSV = os.path.join(SCRIPT_DIR, "train_data", "dataset_full.csv")

    # --- RF 共用参数 ---
    # seq_to_feature 输出维度（固定 28 种 token 类型）
    FEATURE_DIM = 28
    # 滑动窗口大小（计算加权距离）
    WINDOW_SIZE = 5

    # --- RF 参数 ---
    RF_PARAMS = {
        'n_estimators': 20,        # 决策树数量
        'max_depth': 10,           # 最大深度
        'min_samples_split': 2,    # 分裂最小样本数
        'min_samples_leaf': 1,     # 叶节点最小样本数
        'n_jobs': -1,              # 并行线程数（-1 全部）
        'random_state': 42,        # 随机种子
        'class_weight': 'balanced', # 类别权重（自动平衡）
    }

    # --- DL_CNN 参数 ---
    SEQ_DIM = 300          # token 序列最大长度（pad 到此长度）
    VOCAB = 29             # 词表大小（token ID 0~28，共 29 种）
    PAD_ID = 28            # Padding token ID
    EMB_DIM = 32           # Embedding 维度
    CHANNELS = 64          # Conv 通道数
    KERNELS = (2, 3, 4, 5) # 卷积核大小（多尺度特征）
    CNN_HIDDEN = 64        # 全连接隐藏层维度
    CNN_DROPOUT = 0.5      # Dropout 比例

    # --- 训练通用参数 ---
    EPOCHS = 20            # 训练轮数
    BATCH = 256            # 批次大小
    LR = 1e-3              # 学习率
    WD = 1e-4              # 权重衰减（L2 正则）
    TEST_SIZE = 0.2        # 验证集比例
    RANDOM_STATE = 42      # 数据划分随机种子


# ==================== ctypes 绑定 ====================
class KaddEngine:
    """C 动态库统一封装：DFA 分词 + 特征提取

    封装两个 C 库：
    - libkadd_dfa_engine.dll/so: DFA 分词，将 SQL 字符串转换为 token 序列
    - libkadd_feature_engine.dll/so: 滑动窗口特征提取，将 token 序列转换为 28 维向量

    使用流程：
        engine = KaddEngine(dfa_so, feature_so, ...)
        tokens = engine.tokenize(query)      # SQL → token 序列
        feature = engine.to_feature(tokens)  # token 序列 → 28 维向量
        feature = engine.query_to_feature(q) # 一步完成上述两步
        engine.close()
    """

    def __init__(self, dfa_so_path, feature_so_path,
                 transition_path=None, output_path=None, eod_path=None,
                 config_path=None,
                 max_input_length=300, max_output_dim=300,
                 window_size=5, feature_dim=28):
        """初始化并加载动态库

        参数：
            dfa_so_path: DFA 引擎动态库路径
            feature_so_path: 特征引擎动态库路径
            transition_path/output_path/eod_path: DFA 三表路径（命令行指定）
            config_path: 配置文件路径（三表路径的 fallback）
            max_input_length: 最大输入字符长度
            max_output_dim: 最大 token 输出数量
            window_size: 滑动窗口大小（seq_to_feature 使用）
            feature_dim: 特征维度（固定 28）
        """
        # ----- 加载 DFA 引擎库 -----
        self.dfa_lib = ctypes.CDLL(dfa_so_path)
        # 设置函数签名：kadd_dfa_engine_create(engine_pp, paths, max_in, max_out)
        self.dfa_lib.kadd_dfa_engine_create.argtypes = [
            ctypes.POINTER(ctypes.c_void_p),
            ctypes.POINTER(ctypes.c_char_p),
            ctypes.c_size_t, ctypes.c_size_t]
        self.dfa_lib.kadd_dfa_engine_create.restype = ctypes.c_int
        # 设置函数签名：kadd_dfa_engine_tokenize(engine, input, len, output, count)
        self.dfa_lib.kadd_dfa_engine_tokenize.argtypes = [
            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
        self.dfa_lib.kadd_dfa_engine_tokenize.restype = ctypes.c_int
        # kadd_dfa_engine_free 的签名为 (kadd_dfa_engine_t **)：需传句柄变量的地址，
        # C 侧释放后会把该变量写回 NULL（消除悬空句柄）
        self.dfa_lib.kadd_dfa_engine_free.argtypes = [
            ctypes.POINTER(ctypes.c_void_p)]

        # ----- 加载 Feature 引擎库 -----
        self.feat_lib = ctypes.CDLL(feature_so_path)
        # 设置函数签名：kadd_seq_to_feature(seq, seq_len, window, feature, feature_dim)
        self.feat_lib.kadd_seq_to_feature.argtypes = [
            ctypes.POINTER(ctypes.c_int), ctypes.c_int, ctypes.c_int,
            ctypes.POINTER(ctypes.c_int), ctypes.c_int]
        self.feat_lib.kadd_seq_to_feature.restype = ctypes.c_int

        # ----- 确定 DFA 三表路径 -----
        # 优先级：命令行参数 > config.json
        if transition_path and output_path and eod_path:
            trans, out, eod = transition_path, output_path, eod_path
        elif config_path:
            cfg = json.load(open(config_path, encoding='utf-8'))['kadd_dfa_engine']
            trans = cfg['transition_table_path']
            out = cfg['output_table_path']
            eod = cfg['eod_table_path']
        else:
            raise ValueError("Need either table paths or config_path")

        # 构造 C 兼容的路径数组
        paths = (ctypes.c_char_p * 3)(
            trans.encode(), out.encode(), eod.encode())

        # ----- 创建 DFA 引擎实例 -----
        self._dfa_eng = ctypes.c_void_p()
        ret = self.dfa_lib.kadd_dfa_engine_create(
            ctypes.byref(self._dfa_eng),
            ctypes.cast(paths, ctypes.POINTER(ctypes.c_char_p)),
            max_input_length, max_output_dim)
        if ret != 0:
            raise RuntimeError(f"DFA engine create failed: {ret}")

        # 保存配置参数
        self.max_output_dim = max_output_dim
        self.window_size = window_size
        self.feature_dim = feature_dim

    def tokenize(self, query: str) -> np.ndarray:
        """DFA 分词：将 SQL 字符串转换为 token ID 序列

        参数：
            query: SQL 字符串

        返回：
            token ID 数组 (dtype=np.int32)，失败返回空数组
        """
        buf = (ctypes.c_int * self.max_output_dim)()  # 输出缓冲区
        cnt = ctypes.c_int(0)                          # 输出 token 数量
        b = query.encode('utf-8')
        ret = self.dfa_lib.kadd_dfa_engine_tokenize(
            self._dfa_eng, b, len(b), buf, ctypes.byref(cnt))
        if ret != 0:
            return np.array([], dtype=np.int32)  # 失败返回空
        return np.frombuffer(buf, dtype=np.int32)[:cnt.value].copy()

    def to_feature(self, tokens: np.ndarray) -> np.ndarray:
        """特征提取：将 token 序列转换为 28 维特征向量

        滑动窗口算法：对每个 token，计算其与左右窗口内 token 的加权距离
        特征值范围：0 ~ 9789

        参数：
            tokens: token ID 序列

        返回：
            28 维特征向量 (dtype=np.float32)，失败返回零向量
        """
        if len(tokens) == 0:
            return np.zeros(self.feature_dim, dtype=np.float32)
        feat = (ctypes.c_int * self.feature_dim)()       # 输出缓冲区
        seq = (ctypes.c_int * len(tokens))(*tokens.astype(np.int32))
        ret = self.feat_lib.kadd_seq_to_feature(
            seq, len(tokens), self.window_size, feat, self.feature_dim)
        if ret != 0:
            return np.zeros(self.feature_dim, dtype=np.float32)
        return np.frombuffer(feat, dtype=np.int32).copy().astype(np.float32)

    def query_to_feature(self, query: str) -> np.ndarray:
        """一步完成：SQL → token 序列 → 28 维特征向量

        等价于: to_feature(tokenize(query))
        """
        tok = self.tokenize(query)
        return self.to_feature(tok)

    def close(self):
        """释放 DFA 引擎资源"""
        # 传句柄变量地址（byref），而不是句柄本身；释放后 _dfa_eng 复位为
        # NULL 的 c_void_p 对象（而非 None），使 close()/__del__ 可重复调用
        # 且不会出现 byref(None) 的 TypeError。
        if getattr(self, '_dfa_eng', None) is not None and self._dfa_eng.value:
            self.dfa_lib.kadd_dfa_engine_free(ctypes.byref(self._dfa_eng))
        if hasattr(self, '_dfa_eng'):
            self._dfa_eng = ctypes.c_void_p()

    def __del__(self):
        self.close()


# ==================== 数据加载 ====================
def load_raw_csv(csv_path, engine, model_type, cfg):
    """加载原始 CSV 数据并转换为模型输入格式

    流程：
    1. 读取 CSV 文件（自动检测编码）
    2. 遍历每条 Query，根据模型类型转换为特征
       - RF: 调用 seq_to_feature，输出 28 维向量
       - DL_CNN: token pad 到固定长度 (300 维)
    3. 返回特征矩阵 X 和标签向量 y

    参数：
        csv_path: CSV 文件路径（需包含 Query, Label 列）
        engine: KaddEngine 实例
        model_type: 模型类型 ('rf', 'dl_cnn')
        cfg: Config 配置对象

    返回：
        X: 特征矩阵 (n_samples, feature_dim)
        y: 标签向量 (n_samples,)
    """
    # --- 读取 CSV（自动检测编码）---
    encodings = ['utf-8-sig', 'utf-8', 'latin-1', 'gbk', 'gb18030']
    df = None
    for enc in encodings:
        try:
            df = pd.read_csv(csv_path, encoding=enc)
            print(f"CSV encoding: {enc}")
            break
        except (UnicodeDecodeError, UnicodeError):
            continue
    if df is None:
        df = pd.read_csv(csv_path, encoding='utf-8', errors='replace')

    # --- 提取 Query 和 Label 列（大小写不敏感）---
    col_map = {c.lower(): c for c in df.columns}
    queries = df[col_map['query']].astype(str).values
    labels = df[col_map['label']].values.astype(np.int64)

    # --- 遍历每条 Query，转换为特征 ---
    feats, labs = [], []
    skipped = 0
    for i, q in enumerate(queries):
        if model_type == 'rf':
            # RF: SQL → 28 维特征向量
            f = engine.query_to_feature(q)
            if f is None or len(f) == 0:
                skipped += 1
                continue
            feats.append(f)
        else:  # dl_cnn
            # CNN: SQL → token 序列 → pad 到固定长度
            tok = engine.tokenize(q)
            if len(tok) == 0:
                skipped += 1
                continue
            # pad: 不足 300 用 PAD_ID 填充，超过 300 截断
            seq = np.full(cfg.SEQ_DIM, cfg.PAD_ID, dtype=np.float32)
            n = min(len(tok), cfg.SEQ_DIM)
            if n > 0:
                seq[:n] = tok[:n]
            feats.append(seq)
        labs.append(labels[i])

        # 进度打印
        if (i + 1) % 1000 == 0:
            print(f"  progress {i+1}/{len(queries)} (skip {skipped})")

    # --- 转换为 NumPy 数组 ---
    X = np.asarray(feats, dtype=np.float32)
    y = np.asarray(labs, dtype=np.int64)

    # --- 打印统计信息 ---
    print(f"samples {len(X)} (skip {skipped}), dim {X.shape[1]}, label 0={sum(y==0)} 1={sum(y==1)}")
    if model_type == 'rf':
        print(f"feature range: min={X.min():.0f} max={X.max():.0f} mean={X.mean():.1f} std={X.std():.1f}")
    return X, y


def load_json_data(json_path, engine, model_type, cfg):
    """加载 JSON 格式数据（已预分词）

    JSON 格式：
    {
      "data": [
        {"uri": "...", "label": 0, "tokens": [1, 19, 7, ...]},
        ...
      ]
    }

    特点：
    - tokens 已预分词，无需调用 DFA
    - uri 相当于 query，仅用于调试
    - 适合大规模数据集，避免重复分词

    参数：
        json_path: JSON 文件路径
        engine: KaddEngine 实例（仅用于 to_feature）
        model_type: 模型类型 ('rf', 'dl_cnn')
        cfg: Config 配置对象

    返回：
        X: 特征矩阵 (n_samples, feature_dim)
        y: 标签向量 (n_samples,)
    """
    import json

    print(f"Loading JSON: {json_path}")
    with open(json_path, 'r', encoding='utf-8') as f:
        data = json.load(f)

    samples = data.get('data', [])
    print(f"Total samples in JSON: {len(samples)}")

    feats, labs = [], []
    skipped = 0

    for i, item in enumerate(samples):
        tokens = item.get('tokens', [])
        label = item.get('label', -1)

        if len(tokens) == 0 or label < 0:
            skipped += 1
            continue

        tokens_arr = np.array(tokens, dtype=np.int32)

        if model_type == 'rf':
            # RF: tokens → 28 维特征向量
            f = engine.to_feature(tokens_arr)
            if f is None or len(f) == 0:
                skipped += 1
                continue
            feats.append(f)
        else:  # dl_cnn
            # CNN: tokens → pad 到固定长度
            seq = np.full(cfg.SEQ_DIM, cfg.PAD_ID, dtype=np.float32)
            n = min(len(tokens), cfg.SEQ_DIM)
            if n > 0:
                seq[:n] = tokens[:n]
            feats.append(seq)

        labs.append(label)

        # 进度打印
        if (i + 1) % 5000 == 0:
            print(f"  progress {i+1}/{len(samples)} (skip {skipped})")

    # --- 转换为 NumPy 数组 ---
    X = np.asarray(feats, dtype=np.float32)
    y = np.asarray(labs, dtype=np.int64)

    print(f"samples {len(X)} (skip {skipped}), dim {X.shape[1]}, label 0={sum(y==0)} 1={sum(y==1)}")
    if model_type == 'rf':
        print(f"feature range: min={X.min():.0f} max={X.max():.0f} mean={X.mean():.1f} std={X.std():.1f}")
    return X, y


# ==================== 数据集拆分 ====================
def split_raw_dataset(source_csv, train_csv, verify_csv,
                      test_size=0.2, random_state=42):
    """将原始 CSV 拆分为 80% 训练集和 20% 验证集。

    拆分规则：
    1. 先整体打乱行顺序
    2. 按 Query 分组，保证同一 Query 不会同时出现在训练集和验证集
    3. 按 Label 分层，保证两个文件的类别比例一致
    4. 使用固定随机种子，保证结果可复现
    """
    df = pd.read_csv(source_csv, encoding='utf-8-sig')
    lowered = {str(column).lower(): column for column in df.columns}
    if 'query' not in lowered or 'label' not in lowered:
        raise ValueError("source CSV must contain Query and Label columns")

    query_col = lowered['query']
    label_col = lowered['label']
    df = df[[query_col, label_col]].copy()
    df.columns = ['Query', 'Label']
    df['Query'] = df['Query'].fillna('').astype(str)
    df['Label'] = pd.to_numeric(df['Label'], errors='coerce')
    df = df.dropna(subset=['Label'])
    df['Label'] = df['Label'].astype(int)

    if df.empty:
        raise ValueError("source CSV has no valid rows")

    # 先整体打乱行顺序，再进行拆分，保证 train/verify 文件内部顺序也是随机的
    df = df.sample(frac=1.0, random_state=random_state).reset_index(drop=True)

    label_counts = df.groupby('Query', sort=False)['Label'].nunique()
    if (label_counts > 1).any():
        conflicting = label_counts[label_counts > 1].index[:5].tolist()
        raise ValueError(f"source CSV has conflicting labels for queries: {conflicting}")

    group_labels = df.groupby('Query', sort=False)['Label'].first()
    queries = group_labels.index.to_numpy()
    labels = group_labels.to_numpy()

    train_queries, verify_queries = train_test_split(
        queries,
        test_size=test_size,
        random_state=random_state,
        stratify=labels,
    )

    train_set = set(train_queries)
    verify_set = set(verify_queries)
    train_df = df[df['Query'].isin(train_set)].copy()
    verify_df = df[df['Query'].isin(verify_set)].copy()

    if set(train_df['Query']) & set(verify_df['Query']):
        raise AssertionError("internal error: train/verify query leakage")

    train_df.to_csv(train_csv, index=False, encoding='utf-8')
    verify_df.to_csv(verify_csv, index=False, encoding='utf-8')

    print(f"[OK] Dataset split completed")
    print(f"  source: {source_csv}")
    print(f"  train:  {train_csv} ({len(train_df)} rows)")
    print(f"  verify: {verify_csv} ({len(verify_df)} rows)")
    print(f"  train labels: {train_df['Label'].value_counts().sort_index().to_dict()}")
    print(f"  verify labels: {verify_df['Label'].value_counts().sort_index().to_dict()}")


# ==================== 模型定义 ====================
class TextCNN(nn.Module):
    """DL_CNN: token 序列输入 → Embedding → 多尺度 Conv → MaxPool → FC

    网络结构：
        Input(300) → Embedding(32) → Conv1d(64, k=2,3,4,5)
                   → MaxPool → Concat(256) → FC(64) → FC(2)

    特点：
    - 多尺度卷积核 (2,3,4,5) 捕获不同长度模式
    - MaxPool 提取最显著特征
    - Embedding 学习 token 表示
    """

    def __init__(self, cfg: Config):
        super().__init__()
        # Embedding: vocab=29, dim=32, padding_idx=28
        self.emb = nn.Embedding(cfg.VOCAB, cfg.EMB_DIM, padding_idx=cfg.PAD_ID)
        # 多尺度卷积: kernel_size ∈ {2, 3, 4, 5}
        self.convs = nn.ModuleList([
            nn.Conv1d(cfg.EMB_DIM, cfg.CHANNELS, k) for k in cfg.KERNELS
        ])
        # 全连接层: channels*4 → hidden → 2
        self.fc1 = nn.Linear(cfg.CHANNELS * len(cfg.KERNELS), cfg.CNN_HIDDEN)
        self.fc2 = nn.Linear(cfg.CNN_HIDDEN, 2)
        self.drop = nn.Dropout(cfg.CNN_DROPOUT)
        self.drop2 = nn.Dropout(0.3)

    def forward(self, x):
        """前向传播

        参数：x - (batch, 300) token 序列（float 类型，需转换为 long）
        返回：(batch, 2) logits
        """
        x = x.long()                               # float → long
        x = self.emb(x).transpose(1, 2)            # (batch, 300) → (batch, 32, 300)
        # 多尺度卷积 + MaxPool
        feats = [torch.max(torch.relu(c(x)), dim=2).values for c in self.convs]
        x = torch.cat(feats, dim=1)                # (batch, 64*4=256)
        x = self.drop(x)
        x = torch.relu(self.fc1(x))
        x = self.drop2(x)
        return self.fc2(x)


# ==================== RF 训练 ====================
def train_rf(X, y, cfg, output_dir, model_path, X_val=None, y_val=None):
    """随机森林训练并导出 ONNX 和跨平台 KADD-RF

    流程：
    1. 划分训练集/验证集
    2. 训练 RandomForestClassifier
    3. 评估性能（准确率、分类报告、混淆矩阵）
    4. 导出 ONNX 模型（使用 skl2onnx）
    5. 导出可由 x86-64/AArch64/SVE C 引擎直接加载的 KADD-RF 模型
    6. 验证 ONNX 和 KADD-RF 模型正确性

    参数：
        X: 特征矩阵 (n_samples, 28)
        y: 标签向量 (n_samples,)
        cfg: Config 配置对象
        output_dir: 输出目录
        model_path: 完整模型输出路径
    """
    print("\n" + "=" * 60)
    print("Training Random Forest...")

    # --- 划分数据集 ---
    if X_val is None or y_val is None:
        X_tr, X_te, y_tr, y_te = train_test_split(
            X, y, test_size=cfg.TEST_SIZE, random_state=cfg.RANDOM_STATE, stratify=y)
    else:
        X_tr, y_tr = X, y
        X_te, y_te = X_val, y_val

    # 保存与训练严格隔离的验证集。鲲鹏端直接读取该文件，准确率验证和
    # 性能测试使用完全相同的 float32 输入，避免重复特征提取影响计时。
    benchmark_data_path = os.path.splitext(model_path)[0] + ".test.kds"
    export_kadd_dataset(X_te, y_te, benchmark_data_path)
    print(f"[OK] Held-out benchmark data saved: {benchmark_data_path}")

    # --- 训练模型 ---
    model = RandomForestClassifier(**cfg.RF_PARAMS)
    model.fit(X_tr, y_tr)

    # --- 评估 ---
    y_pred = model.predict(X_te)
    print(f"Validation Accuracy: {accuracy_score(y_te, y_pred):.4f}")
    print(classification_report(y_te, y_pred, target_names=['Label 0', 'Label 1']))
    print("Confusion Matrix:\n", confusion_matrix(y_te, y_pred))

    # --- 导出跨平台 KADD-RF ---
    native_model_path = os.path.splitext(model_path)[0] + ".krf"
    export_kadd_rf(model, native_model_path)
    parity_sample = X_te[:min(len(X_te), 4096)].astype(np.float32, copy=False)
    native_diff = assert_rf_export_parity(
        model, native_model_path, parity_sample
    )
    print(f"[OK] KADD-RF saved: {native_model_path}")
    print(f"[parity] sklearn vs KADD-RF max diff = {native_diff:.2e}")

    # --- 导出 ONNX ---
    try:
        from skl2onnx import convert_sklearn
        from skl2onnx.common.data_types import FloatTensorType
        # 定义输入类型：(batch, 28) float32
        initial_type = [('input', FloatTensorType([None, cfg.FEATURE_DIM]))]
        onnx_model = convert_sklearn(
            model, initial_types=initial_type, target_opset=12,
            options={id(model): {'zipmap': False}})  # 输出数组而非字典
        onnx.save(onnx_model, model_path)
        print(f"[OK] ONNX saved: {model_path}")

        # --- 验证 ONNX ---
        sess = ort.InferenceSession(model_path)
        pred = sess.run(None, {'input': X_te[:5].astype(np.float32)})[0]
        print(f"[OK] ONNX verified, predict: {pred}")
    except ImportError:
        print("Install skl2onnx: pip install skl2onnx")


# ==================== DL 训练 ====================
def train_dl(X, y, model_type, cfg, output_dir, model_path, X_val=None, y_val=None):
    """PyTorch 深度学习训练（TextCNN）

    流程：
    1. 划分训练集/验证集
    2. 创建模型（TextCNN）
    3. 训练：使用类别权重平衡、学习率调度、早停
    4. 评估：AUC、F1、准确率、混淆矩阵
    5. 导出 ONNX（包含 Softmax 层）
    6. 验证 PyTorch vs ONNX 一致性

    参数：
        X: 特征矩阵 (n_samples, feature_dim)
        y: 标签向量 (n_samples,)
        model_type: 'dl_cnn'
        cfg: Config 配置对象
        output_dir: 输出目录
        model_path: 完整模型输出路径
    """
    print("\n" + "=" * 60)
    print(f"Training {model_type.upper()}...")

    # --- 划分数据集 ---
    if X_val is None or y_val is None:
        X_tr, X_te, y_tr, y_te = train_test_split(
            X, y, test_size=cfg.TEST_SIZE, random_state=cfg.RANDOM_STATE, stratify=y)
    else:
        X_tr, y_tr = X, y
        X_te, y_te = X_val, y_val
    print(f"Train {len(X_tr)}, Test {len(X_te)}")

    if model_type == 'dl_cnn':
        benchmark_data_path = os.path.splitext(model_path)[0] + ".test.kds"
        export_kadd_dataset(X_te, y_te, benchmark_data_path)
        print(f"[OK] Held-out CNN benchmark data saved: {benchmark_data_path}")

    # --- 类别权重（平衡样本不均衡）---
    counts = np.bincount(y_tr, minlength=2).astype(np.float32)
    weights = len(y_tr) / (2.0 * counts)
    print(f"class weights: {weights}")

    # --- 创建模型 ---
    device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
    model = TextCNN(cfg)
    model = model.to(device)

    # --- 优化器和调度器 ---
    optimizer = torch.optim.Adam(model.parameters(), lr=cfg.LR, weight_decay=cfg.WD)
    scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(
        optimizer, mode='max', factor=0.5, patience=2)

    # --- 损失函数（带类别权重）---
    loss_fn = nn.CrossEntropyLoss(
        weight=torch.tensor(weights, device=device, dtype=torch.float32))

    # --- 数据转 Tensor ---
    Xtr = torch.from_numpy(X_tr).to(device)
    ytr = torch.from_numpy(y_tr).to(device)
    Xte = torch.from_numpy(X_te).to(device)
    yte = torch.from_numpy(y_te).to(device)

    # --- 训练循环 ---
    best_auc = 0.0
    best_accuracy = 0.0 
    best_state = None
    patience, bad = 2, 0  # 早停：连续 2 轮无提升则停止
    n = len(Xtr)

    for ep in range(cfg.EPOCHS):
        # 训练阶段
        model.train()
        perm = torch.randperm(n, device=device)  # 随机打乱
        for i in range(0, n, cfg.BATCH):
            idx = perm[i:i+cfg.BATCH]
            logits = model(Xtr[idx])
            loss = loss_fn(logits, ytr[idx])
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()

        # 验证阶段
        model.eval()
        with torch.no_grad():
            probs = torch.softmax(model(Xte), dim=1)[:, 1].cpu().numpy()
        pred = (probs >= 0.5).astype(int)
        accuracy = accuracy_score(y_te, pred)
        auc = roc_auc_score(y_te, probs)
        f1 = f1_score(y_te, (probs >= 0.5).astype(int))
        print(f"epoch {ep+1:2d}: val AUC={auc:.4f} F1={f1:.4f} accuracy={accuracy:.4f}")

        # 学习率调度
        scheduler.step(auc)
        if accuracy > best_accuracy+0.001:
            best_accuracy = accuracy
            best_state = {k: v.clone() for k, v in model.state_dict().items()}
            bad = 0
        else:
            bad += 1
            if bad >= patience:
                print(f"early stop @ ep{ep+1}")
                break

        # 保存最佳模型
        # if auc > best_auc:
        #     best_auc = auc
        #     best_state = {k: v.clone() for k, v in model.state_dict().items()}
        #     bad = 0
        # else:
        #     bad += 1
        #     if bad >= patience:
        #         print(f"early stop @ ep{ep+1}")
        #         break

    # --- 加载最佳模型 ---
    model.load_state_dict(best_state)
    model.eval()
    with torch.no_grad():
        probs = torch.softmax(model(Xte), dim=1)[:, 1].cpu().numpy()
    pred = (probs >= 0.5).astype(int)

    # --- 最终评估 ---
    print("\n=== Validation ===")
    print(f"Accuracy: {accuracy_score(y_te, pred):.4f}")
    print(f"AUC: {roc_auc_score(y_te, probs):.4f}")
    print(classification_report(y_te, pred, target_names=['Label 0', 'Label 1']))
    print("Confusion Matrix:\n", confusion_matrix(y_te, pred))

    # --- 导出 ONNX（包含 Softmax）---
    model_cpu = model.cpu().eval()

    # Wrapper: 在模型后添加 Softmax，输出概率而非 logits
    class _ProbWrapper(nn.Module):
        def __init__(self, m):
            super().__init__()
            self.m = m
        def forward(self, x):
            return torch.softmax(self.m(x), dim=1)

    wrapper = _ProbWrapper(model_cpu).eval()
    dummy = torch.zeros(1, X.shape[1], dtype=torch.float32)
    torch.onnx.export(
        wrapper, dummy, model_path,
        input_names=['input'], output_names=['prob'],
        dynamic_axes={'input': {0: 'batch'}, 'prob': {0: 'batch'}},
        opset_version=13, dynamo=False)
    print(f"[OK] ONNX saved: {model_path}")

    if model_type == 'dl_cnn':
        native_model_path = os.path.splitext(model_path)[0] + ".kcn"
        export_kadd_cnn(model_path, native_model_path)
        native_diff = assert_cnn_export_parity(
            model_path,
            native_model_path,
            X_te[:min(len(X_te), 128)].astype(np.float32, copy=False),
        )
        print(f"[OK] KADD-CNN saved: {native_model_path}")
        print(f"[parity] ONNX vs KADD-CNN max diff = {native_diff:.2e}")

    # --- ONNX 一致性验证 ---
    sess = ort.InferenceSession(model_path)
    sample = X_te[:32].astype(np.float32)
    with torch.no_grad():
        t_out = torch.softmax(model_cpu(torch.from_numpy(sample)), dim=1).numpy()
    o_out = sess.run(None, {'input': sample})[0]
    diff = np.abs(t_out - o_out).max()
    print(f"[parity] torch vs ort max diff = {diff:.2e} ({'OK' if diff < 1e-4 else 'FAIL'})")


# ==================== 主函数 ====================
def main():
    cfg = Config()
    p = argparse.ArgumentParser(description='SQLi Detection Model Training')
    p.add_argument('--model-type', choices=['rf', 'dl_cnn'], default='rf',
                   help='Model type: rf (Random Forest), dl_cnn (TextCNN)')
    p.add_argument('--data-format', choices=['csv', 'json'], default='csv',
                   help='Data format: csv (Query, Label) or json (pre-tokenized)')
    p.add_argument('--data', default=cfg.RAW_CSV,
                   help='Training data file path (CSV or JSON)')
    p.add_argument('--verify-data', default=cfg.VERIFY_CSV,
                   help='Held-out verification data file path (never used for training)')
    p.add_argument('--source-data', default=cfg.FULL_CSV,
                   help='Full source CSV used by --split-only')
    p.add_argument('--split-only', action='store_true',
                   help='Split --source-data into train_data.csv and verify_data.csv, then exit')
    p.add_argument('--no-verify', action='store_true',
                   help='Ignore --verify-data and split training data internally')
    p.add_argument('--trees', type=int, default=None,
                   help='Override RF n_estimators')
    p.add_argument('--max-depth', type=int, default=None,
                   help='Override RF max_depth')
    p.add_argument('--dfa-so', default=cfg.DFA_SO, help='DFA engine dll/so path')
    p.add_argument('--feature-so', default=cfg.FEATURE_SO, help='Feature engine dll/so path')
    p.add_argument('--transition', default=None, help='DFA transition table path (override config)')
    p.add_argument('--output-table', default=None, help='DFA output table path (override config)')
    p.add_argument('--eod', default=None, help='DFA eod table path (override config)')
    p.add_argument('--config', default=cfg.CONFIG_JSON, help='config.json path')
    p.add_argument(
        '--model-output',
        default=None,
        help='Explicit ONNX output path (recommended for local/Windows training)',
    )
    p.add_argument('--epochs', type=int, default=cfg.EPOCHS, help='Training epochs (DL only)')
    args = p.parse_args()

    if args.split_only:
        split_raw_dataset(
            args.source_data,
            cfg.RAW_CSV,
            cfg.VERIFY_CSV,
            test_size=cfg.TEST_SIZE,
            random_state=cfg.RANDOM_STATE,
        )
        return 0

    if args.trees is not None:
        if args.trees <= 0:
            p.error('--trees must be positive')
        cfg.RF_PARAMS['n_estimators'] = args.trees
    if args.max_depth is not None:
        if args.max_depth <= 0:
            p.error('--max-depth must be positive')
        cfg.RF_PARAMS['max_depth'] = args.max_depth

    # --- 读取 config.json ---
    with open(args.config, 'r', encoding='utf-8') as f:
        config = json.load(f)

    base_path = config.get('base_path', '/opt/KADD/SQLi')

    # --- 确定 ONNX 输出路径（兼容平铺部署配置和旧版嵌套训练配置）---
    base_clean = base_path.rstrip('/\\')
    if args.model_output:
        model_full_path = os.path.abspath(args.model_output)
    else:
        flat_model_keys = {
            'rf': 'model_rf',
            'dl_cnn': 'model_cnn',
        }
        nested_engine_keys = {
            'rf': 'ml_engine',
            'dl_cnn': 'cnn_engine',
        }
        flat_key = flat_model_keys[args.model_type]
        if flat_key in config:
            configured_path = config[flat_key]
        else:
            configured_path = config[nested_engine_keys[args.model_type]]['model']
        if os.path.isabs(configured_path):
            model_full_path = configured_path
        else:
            model_full_path = os.path.join(base_clean, configured_path)

    # 部署配置可让 CNN 直接指向 .kcn；训练仍先生成同名 .onnx，
    # 随后 train_dl 会在旁边导出 .kcn。
    if args.model_type == 'dl_cnn' and \
            os.path.splitext(model_full_path)[1].lower() == '.kcn':
        model_full_path = os.path.splitext(model_full_path)[0] + '.onnx'

    # 输出目录
    output_dir = os.path.dirname(model_full_path)
    os.makedirs(output_dir, exist_ok=True)

    # --- 确定 DFA 三表路径 ---
    local_table_dir = os.path.join(cfg.SCRIPT_DIR, '../../assets/data')
    if args.transition:
        trans, out_tbl, eod_tbl = args.transition, args.output_table, args.eod
        config_to_use = None
    elif platform.system() == 'Windows':
        # Windows 本地测试：用 assets/data/*.bin
        trans = os.path.join(local_table_dir, 'transition_table.bin')
        out_tbl = os.path.join(local_table_dir, 'output_table.bin')
        eod_tbl = os.path.join(local_table_dir, 'eod_table.bin')
        config_to_use = None
        print(f"[Windows] Using local table paths: {trans}")
    else:
        # Linux: 用 config.json 中的 paths
        if 'paths' in config:
            paths = config['paths']
            trans = os.path.join(
                base_clean, paths['transition_table'].lstrip('/\\'))
            out_tbl = os.path.join(
                base_clean, paths['output_table'].lstrip('/\\'))
            eod_tbl = os.path.join(
                base_clean, paths['eod_table'].lstrip('/\\'))
        else:
            trans = config['transition_table']
            out_tbl = config['output_table']
            eod_tbl = config['eod_table']
        config_to_use = None

    # --- 从 config 读取训练参数 ---
    dfa_cfg = config.get('dfa_engine', {
        'max_input_chars': cfg.SEQ_DIM,
        'max_tokens': cfg.SEQ_DIM,
    })
    feature_cfg = config.get('feature_engine', {
        'window_size': cfg.WINDOW_SIZE,
        'feature_dim': cfg.FEATURE_DIM,
    })

    print(f"Model type: {args.model_type}")
    print(f"Data format: {args.data_format}")
    print(f"Data file: {args.data}")
    if not args.no_verify:
        print(f"Verify data: {args.verify_data}")
    print(f"Model output: {model_full_path}")
    if trans:
        print(f"Transition: {trans}")
        print(f"Output table: {out_tbl}")
        print(f"EOD: {eod_tbl}")

    # --- 创建引擎 ---
    # JSON 格式如果只用于 CNN，可以不需要 DFA 引擎（但 RF 仍需 feature 引擎）
    engine = KaddEngine(args.dfa_so, args.feature_so,
                        transition_path=trans,
                        output_path=out_tbl,
                        eod_path=eod_tbl,
                        config_path=config_to_use,
                        max_input_length=dfa_cfg['max_input_chars'],
                        max_output_dim=dfa_cfg['max_tokens'],
                        window_size=feature_cfg['window_size'],
                        feature_dim=feature_cfg['feature_dim'])
    try:
        # --- 根据数据格式加载 ---
        if args.data_format == 'json':
            X, y = load_json_data(args.data, engine, args.model_type, cfg)
            if not args.no_verify and os.path.exists(args.verify_data):
                X_val, y_val = load_json_data(args.verify_data, engine, args.model_type, cfg)
            else:
                X_val, y_val = None, None
        else:
            X, y = load_raw_csv(args.data, engine, args.model_type, cfg)
            if not args.no_verify and os.path.exists(args.verify_data):
                X_val, y_val = load_raw_csv(args.verify_data, engine, args.model_type, cfg)
            else:
                X_val, y_val = None, None
    finally:
        engine.close()

    if len(X) == 0:
        print("[ERROR] No valid samples")
        return 1

    # --- 训练 ---
    if args.model_type == 'rf':
        train_rf(X, y, cfg, output_dir, model_full_path, X_val, y_val)
    else:
        cfg.EPOCHS = args.epochs
        train_dl(X, y, args.model_type, cfg, output_dir, model_full_path, X_val, y_val)

    print("\nTraining completed!")
    return 0


if __name__ == '__main__':
    sys.exit(main())

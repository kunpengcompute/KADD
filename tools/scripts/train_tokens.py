"""Token 分类模型训练脚本 — 随机森林，导出 ONNX 格式"""

import json
import os
from collections import Counter
from typing import Tuple, Dict, Any

import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
from sklearn.model_selection import train_test_split


# ==================== 配置参数 ====================
class Config:
    SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
    DATA_PATH = os.path.join(os.path.dirname(SCRIPT_DIR), "assets", "data", "full_dfa_tokenize_results.json")
    CSV_PATH = os.path.join(os.path.dirname(SCRIPT_DIR), "assets", "data", "total_training_dataset_v4_pinnacle.csv")

    WINDOW_SIZE = 5
    FEATURE_DIM = 28

    RF_PARAMS = {
        'n_estimators': 10,
        'max_depth': 8,
        'min_samples_split': 2,
        'min_samples_leaf': 1,
        'n_jobs': -1,
        'random_state': 42,
        'class_weight': 'balanced',
    }
    TEST_SIZE = 0.2
    RANDOM_STATE = 12
    VERIFY_SIZE = 100


# ==================== 特征提取 ====================
def sliding_window_distance(seq: np.ndarray, window_size: int) -> np.ndarray:
    """对应 C 代码中的 sliding_window_distance_shift_fast"""
    n = len(seq)
    result = np.zeros(n, dtype=np.int32)
    for d in range(1, window_size):
        if d >= n:
            break
        weight = window_size - d
        weighted = seq * weight
        result[:n-d] += weighted[d:]
        result[d:] += weighted[:n-d]
    return result


def feature_vector_single(tokens: np.ndarray, window_size: int = 5) -> np.ndarray:
    """将 tokens 序列转换为 FEATURE_DIM 维特征向量，对应 C 代码 kadd_seq_to_feature"""
    if len(tokens) == 0:
        return np.zeros(Config.FEATURE_DIM, dtype=np.int32)

    result = sliding_window_distance(tokens, window_size)
    final_result = np.zeros(Config.FEATURE_DIM, dtype=np.int32)
    for i, token_id in enumerate(tokens):
        if 0 <= token_id < Config.FEATURE_DIM:
            final_result[token_id] += result[i]
    return final_result


# ==================== 数据加载 ====================
def load_csv_data(csv_path: str) -> Tuple[list, np.ndarray]:
    """从 CSV 加载 queries 和 labels，自动尝试多种编码"""
    import pandas as pd

    encodings = ['utf-8', 'utf-8-sig', 'latin-1', 'gbk', 'gb18030']
    df = None
    for enc in encodings:
        try:
            df = pd.read_csv(csv_path, encoding=enc)
            print(f"CSV 编码: {enc}")
            break
        except (UnicodeDecodeError, UnicodeError):
            continue
    if df is None:
        df = pd.read_csv(csv_path, encoding='utf-8', errors='replace')

    col_map = {c.lower(): c for c in df.columns}
    queries = df[col_map['query']].tolist()
    labels = df[col_map['label']].tolist()

    print(f"CSV 数据: {len(queries)} 样本, 0={labels.count(0)}, 1={labels.count(1)}")
    return queries, np.array(labels, dtype=np.int32)


def tokenize_queries(queries: list) -> list:
    """对查询列表进行 DFA 分词"""
    import sys
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    dfa_dir = os.path.join(project_root, 'tools', 'profile_data')
    sys.path.insert(0, dfa_dir)

    import dfa_engine
    engine = dfa_engine.DFAEngine()

    token_lists = []
    for query in queries:
        try:
            tokens = engine.tokenize(query)
            token_ids = [dfa_engine.DFAEngine.char_type_to_index(t.type) for t in tokens]
            token_lists.append(token_ids)
        except Exception as e:
            print(f"警告: 分词失败 - {query[:50]}... ({e})")
            token_lists.append([])

    return token_lists


def load_data_from_csv(csv_path: str, window_size: int = 5) -> Tuple[np.ndarray, np.ndarray]:
    """从 CSV 加载 → DFA 分词 → 特征提取"""
    queries, labels = load_csv_data(csv_path)

    print("\n正在进行 DFA 分词...")
    token_lists = tokenize_queries(queries)

    X, y = [], []
    for tokens, label in zip(token_lists, labels):
        if len(tokens) == 0:
            continue
        features = feature_vector_single(np.array(tokens, dtype=np.int32), window_size)
        X.append(features)
        y.append(label)

    print(f"特征提取: {len(X)} 有效样本, 标签分布: {Counter(y)}")
    return np.array(X, dtype=np.int32), np.array(y, dtype=np.int32)


def load_data(data_path: str, window_size: int = 5, dedup_strategy: str = "balanced") -> Tuple[np.ndarray, np.ndarray]:
    """从 JSON 加载并提取特征。dedup_strategy: none/all/balanced"""
    with open(data_path, 'r', encoding='utf-8') as f:
        content = f.read().split("处理完成")[0]
        data = json.loads(content)

    seen = set() if dedup_strategy != "none" else None
    X, y = [], []

    for item in data['data']:
        label = item['label']
        tokens = item['tokens_correct'] if dedup_strategy == "balanced" else item['tokens']
        key = tuple(tokens)

        if seen is not None and key in seen:
            continue
        if seen is not None:
            seen.add(key)

        features = feature_vector_single(np.array(tokens, dtype=np.int32), window_size)
        X.append(features)
        y.append(label)

    print(f"数据加载 ({dedup_strategy}): {len(X)}/{len(data['data'])} 样本, 标签: {Counter(y)}")
    return np.array(X, dtype=np.int32), np.array(y, dtype=np.int32)


def prepare_datasets(X, y, test_size=0.2, random_state=12, verify_size=100) -> Dict[str, Any]:
    """划分训练集、测试集和验证集"""
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=test_size, random_state=random_state, stratify=y
    )
    X_verify = X_test[:verify_size].astype(np.float32)
    y_verify = y_test[:verify_size].astype(np.int32)

    print(f"Train: {X_train.shape[0]}, Test: {X_test.shape[0]}, Verify: {verify_size}")
    return {
        'X_train': X_train, 'X_test': X_test,
        'y_train': y_train, 'y_test': y_test,
        'X_verify': X_verify, 'y_verify': y_verify,
    }


# ==================== 训练与评测 ====================
def train_and_evaluate(datasets):
    """训练随机森林并输出评测结果"""
    print("=" * 60)
    print("训练随机森林模型...")

    model = RandomForestClassifier(**Config.RF_PARAMS)
    model.fit(datasets['X_train'], datasets['y_train'])

    # 准确率
    y_train_pred = model.predict(datasets['X_train'])
    y_test_pred = model.predict(datasets['X_test'])
    y_prob = model.predict_proba(datasets['X_verify'])[:, 1]

    for name, preds, true in [
        ('Train', y_train_pred, datasets['y_train']),
        ('Test', y_test_pred, datasets['y_test']),
        ('Verify', (y_prob >= 0.5).astype(int), datasets['y_verify']),
    ]:
        print(f"  {name} Accuracy: {accuracy_score(true, preds):.4f}")

    # 分类报告与混淆矩阵
    print("\n分类报告 (Test):")
    print(classification_report(datasets['y_test'], y_test_pred, target_names=['Label 0', 'Label 1']))
    print("混淆矩阵:")
    print(confusion_matrix(datasets['y_test'], y_test_pred))

    return model


# ==================== 模型导出 ====================
def export_onnx(model, X_test, y_test, output_dir):
    """导出 ONNX 格式模型并验证"""
    try:
        from skl2onnx import convert_sklearn
        from skl2onnx.common.data_types import FloatTensorType
        import onnx
        import onnxruntime as ort
    except ImportError:
        print("提示: pip install skl2onnx onnxruntime")
        return

    target_opset = 12
    print(f"\n导出 ONNX (opset {target_opset})...")

    initial_type = [('float_input', FloatTensorType([None, Config.FEATURE_DIM]))]
    onnx_model = convert_sklearn(
        model, initial_types=initial_type, target_opset=target_opset,
        options={id(model): {'zipmap': False}}
    )

    onnx_path = os.path.join(output_dir, "model_tokens_rf.onnx")
    onnx.save(onnx_model, onnx_path)
    print(f"[OK] ONNX 模型已保存: {onnx_path}")

    # 验证 ONNX 模型
    try:
        sess = ort.InferenceSession(onnx_path)
        pred = sess.run(None, {'float_input': X_test[:22].astype(np.float32)})[0]
        print(f"[OK] ONNX 验证通过, 预测: {pred}")
    except Exception as e:
        print(f"[ERROR] ONNX 验证失败: {e}")


# ==================== 主流程 ====================
def main():
    import argparse

    parser = argparse.ArgumentParser(description='训练 SQL 注入检测模型')
    parser.add_argument('--csv', type=str, help='使用 CSV 数据集训练')
    parser.add_argument('--json', type=str, help='使用 JSON 数据集训练')
    args = parser.parse_args()

    if args.csv:
        data_path, use_csv = args.csv, True
    elif args.json:
        data_path, use_csv = args.json, False
    else:
        data_path, use_csv = Config.CSV_PATH, True

    print(f"数据源: {data_path} ({'CSV' if use_csv else 'JSON'})")

    # 加载与特征提取
    if use_csv:
        X, y = load_data_from_csv(data_path, Config.WINDOW_SIZE)
    else:
        X, y = load_data(data_path, Config.WINDOW_SIZE, dedup_strategy="balanced")

    print(f"X={X.shape}, y={y.shape}, 0={sum(y==0)}, 1={sum(y==1)}")

    # 划分数据集
    datasets = prepare_datasets(X, y, test_size=Config.TEST_SIZE, random_state=Config.RANDOM_STATE, verify_size=Config.VERIFY_SIZE)

    # 数据诊断
    print(f"X_train: {datasets['X_train'].shape}, 范围 {datasets['X_train'].min()}~{datasets['X_train'].max()}, 均值 {datasets['X_train'].mean():.2f}")
    print(f"y_train: {Counter(datasets['y_train'])}")

    # 训练与评测
    model = train_and_evaluate(datasets)

    # 导出 ONNX
    output_dir = os.path.dirname(os.path.abspath(__file__))
    export_onnx(model, datasets['X_test'], datasets['y_test'], output_dir)

    print("\n训练完成！")


if __name__ == "__main__":
    main()
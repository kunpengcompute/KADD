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
    # 向量CSV路径（由C代码生成）
    # 现在向量CSV在 train_data 子目录下
    TRAIN_DATA_DIR = os.path.join(SCRIPT_DIR, "train_data")
    CSV_VECTORS_PATH = os.path.join(TRAIN_DATA_DIR, "training_vectors.csv")

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

# ==================== 数据加载 ====================
def load_vectors_from_csv(csv_path: str) -> Tuple[np.ndarray, np.ndarray]:
    """从向量CSV加载特征向量和标签（向量已由C代码生成）"""
    import pandas as pd

    encodings = ['utf-8', 'utf-8-sig', 'latin-1', 'gbk', 'gb18030']
    df = None
    for enc in encodings:
        try:
            df = pd.read_csv(csv_path, encoding=enc)
            print(f"向量CSV 编码: {enc}")
            break
        except (UnicodeDecodeError, UnicodeError):
            continue
    if df is None:
        df = pd.read_csv(csv_path, encoding='utf-8', errors='replace')

    # 向量列名: feature_0, feature_1, ..., feature_27
    feature_cols = [f'feature_{i}' for i in range(Config.FEATURE_DIM)]

    # 检查列是否存在
    missing_cols = [col for col in feature_cols if col not in df.columns]
    if missing_cols:
        raise ValueError(f"向量CSV缺少特征列: {missing_cols}")

    # 提取特征向量和标签
    X = df[feature_cols].values.astype(np.float32)
    col_map = {c.lower(): c for c in df.columns}
    y = df[col_map['label']].values.astype(np.int32)

    print(f"向量数据: {len(X)} 样本, 特征维度: {X.shape[1]}, 标签分布: 0={sum(y==0)}, 1={sum(y==1)}")
    return X, y

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
    parser.add_argument('--csv', type=str, help='使用向量CSV数据集训练（由C代码生成）')
    args = parser.parse_args()

    # 默认使用向量CSV路径
    csv_path = args.csv if args.csv else Config.CSV_VECTORS_PATH
    print(f"数据源: {csv_path} (向量CSV)")

    # 直接加载向量数据（特征已由C代码提取）
    X, y = load_vectors_from_csv(csv_path)

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
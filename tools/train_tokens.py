"""
Token 分类模型训练脚本
支持随机森林算法，导出 ONNX 格式
"""

import numpy as np
from sklearn.model_selection import train_test_split
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
import json
import os
from typing import Tuple, Dict, Any

# ==================== 配置参数 ====================
class Config:
    """训练配置参数"""
    # 数据路径
    SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
    DATA_PATH = os.path.join(os.path.dirname(SCRIPT_DIR), "assets", "data", "parsed_results_fixed.json")

    # 特征提取参数
    WINDOW_SIZE = 5
    FEATURE_DIM = 28

    # 模型参数
    RF_PARAMS = {
        'n_estimators': 10,
        'max_depth': 8,
        'min_samples_split': 2,
        'min_samples_leaf': 1,
        'n_jobs': -1,
        'random_state': 42,
        'class_weight': 'balanced',
    }

    # 数据划分参数
    TEST_SIZE = 0.2
    RANDOM_STATE = 12

    # 验证集大小
    VERIFY_SIZE = 100


# ==================== 特征提取 ====================
def sliding_window_distance(seq: np.ndarray, window_size: int) -> np.ndarray:
    """
    滑动窗口距离算法
    对应 C 代码中的 sliding_window_distance_shift_fast

    Args:
        seq: token 序列
        window_size: 窗口大小

    Returns:
        距离数组
    """
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
    """
    将 tokens 序列转换为 28 维特征向量
    对应 C 代码中的 kadd_seq_to_feature

    Args:
        tokens: token ID 数组
        window_size: 窗口大小

    Returns:
        28 维特征向量
    """
    if len(tokens) == 0:
        return np.zeros(Config.FEATURE_DIM, dtype=np.int32)

    # 计算滑窗距离
    result = sliding_window_distance(tokens, window_size)

    # 映射到固定维度特征向量（与 C 代码保持一致）
    final_result = np.zeros(Config.FEATURE_DIM, dtype=np.int32)

    for i, token_id in enumerate(tokens):
        if 0 <= token_id < Config.FEATURE_DIM:
            final_result[token_id] += result[i]  # 使用滑窗距离结果，不是 token_id

    return final_result


# ==================== 数据加载 ====================
def load_data(data_path: str, window_size: int = 5,
              dedup_strategy: str = "balanced") -> Tuple[np.ndarray, np.ndarray]:
    """
    加载并预处理数据

    Args:
        data_path: 数据文件路径
        window_size: 滑动窗口大小
        dedup_strategy: 去重策略
            - "none": 不去重，保留所有数据
            - "all": 完全去重（基于 tokens）
            - "balanced": 平衡去重（只去重攻击样本，保留正常样本重复）

    Returns:
        (X, y) 特征矩阵和标签
    """
    with open(data_path, 'r', encoding='utf-8') as f:
        content = f.read().split("处理完成")[0]
        data = json.loads(content)

    X = []
    y = []

    if dedup_strategy == "balanced":
        # 平衡去重：只对攻击样本去重，保留正常样本的重复
        # 这样可以保持正常流量"重复且固定"的特征
        seen_attack_tokens = set()

        for item in data['data']:
            label = item['label']
            tokens_tuple = tuple(item['tokens'])

            # 只对攻击样本去重
            if label == 1:
                if tokens_tuple in seen_attack_tokens:
                    continue
                seen_attack_tokens.add(tokens_tuple)

            features = feature_vector_single(
                np.array(item['tokens'], dtype=np.int32),
                window_size
            )

            X.append(features)
            y.append(label)

    elif dedup_strategy == "all":
        # 完全去重
        seen_tokens = set()

        for item in data['data']:
            label = item['label']
            tokens_tuple = tuple(item['tokens'])

            if tokens_tuple in seen_tokens:
                continue
            seen_tokens.add(tokens_tuple)

            features = feature_vector_single(
                np.array(item['tokens'], dtype=np.int32),
                window_size
            )

            X.append(features)
            y.append(label)

    else:  # "none"
        # 不去重
        for item in data['data']:
            label = item['label']
            features = feature_vector_single(
                np.array(item['tokens'], dtype=np.int32),
                window_size
            )

            X.append(features)
            y.append(label)

    from collections import Counter
    print(f"数据加载完成 (去重策略: {dedup_strategy})")
    print(f"样本数: {len(X)} / 原始: {len(data['data'])}")
    print(f"标签分布: {Counter(y)}")

    return np.array(X, dtype=np.int32), np.array(y, dtype=np.int32)


def prepare_datasets(
    X: np.ndarray,
    y: np.ndarray,
    test_size: float = 0.8,
    random_state: int = 12,
    verify_size: int = 100
) -> Dict[str, Any]:
    """
    划分训练集、测试集和验证集

    Args:
        X: 特征矩阵
        y: 标签
        test_size: 测试集比例
        random_state: 随机种子
        verify_size: 验证集大小

    Returns:
        包含各数据集的字典
    """
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=test_size, random_state=random_state, stratify=y
    )

    X_verify = X_test[:verify_size].astype(np.float32)
    y_verify = y_test[:verify_size].astype(np.int32)

    print(f"Train: {X_train.shape[0]}, Test: {X_test.shape[0]}, Verify: {verify_size}")

    return {
        'X_train': X_train,
        'X_test': X_test,
        'y_train': y_train,
        'y_test': y_test,
        'X_verify': X_verify,
        'y_verify': y_verify,
    }


# ==================== 模型训练 ====================
def train_random_forest(X_train: np.ndarray, y_train: np.ndarray) -> RandomForestClassifier:
    """
    训练随机森林模型

    Args:
        X_train: 训练特征
        y_train: 训练标签

    Returns:
        训练好的模型
    """
    model = RandomForestClassifier(**Config.RF_PARAMS)
    model.fit(X_train, y_train)
    return model


def train_model(datasets: Dict[str, Any]) -> Dict[str, Any]:
    """
    训练随机森林模型

    Args:
        datasets: 数据集字典

    Returns:
        包含模型的字典
    """
    models = {}

    print("=" * 60)
    print("训练随机森林模型...")
    print("=" * 60)
    models['random_forest'] = train_random_forest(
        datasets['X_train'],
        datasets['y_train']
    )

    return models


# ==================== 模型评测 ====================
def evaluate_model(
    model: Any,
    datasets: Dict[str, Any],
    model_name: str
) -> Dict[str, float]:
    """
    评测单个模型

    Args:
        model: 模型对象
        datasets: 数据集字典
        model_name: 模型名称

    Returns:
        准确率字典
    """
    y_train_pred = model.predict(datasets['X_train'])
    y_test_pred = model.predict(datasets['X_test'])
    y_prob = model.predict_proba(datasets['X_verify'])[:, 1]

    scores = {
        'train': accuracy_score(datasets['y_train'], y_train_pred),
        'test': accuracy_score(datasets['y_test'], y_test_pred),
        'verify': accuracy_score(
            datasets['y_verify'],
            (y_prob >= 0.5).astype(int)
        ),
    }

    # 打印结果
    print(f"\n{model_name}:")
    for dataset, score in scores.items():
        print(f"  {dataset.capitalize()} Accuracy: {score:.4f}")

    return scores


def print_classification_reports(
    models: Dict[str, Any],
    datasets: Dict[str, Any]
) -> None:
    """
    打印详细的分类报告和混淆矩阵

    Args:
        models: 模型字典
        datasets: 数据集字典
    """
    y_test = datasets['y_test']

    for name, model in models.items():
        display_name = 'Random Forest'

        # 分类报告
        y_pred = model.predict(datasets['X_test'])
        print("\n" + "=" * 60)
        print(f"{display_name} 分类报告 (Test):")
        print("=" * 60)
        print(classification_report(
            y_test, y_pred,
            target_names=['Label 0', 'Label 1']
        ))

        # 混淆矩阵
        print("\n" + "=" * 60)
        print("混淆矩阵对比")
        print("=" * 60)
        print(f"{display_name}:")
        print(confusion_matrix(y_test, y_pred))


def evaluate_model_performance(
    models: Dict[str, Any],
    datasets: Dict[str, Any]
) -> Dict[str, Dict[str, float]]:
    """
    评估模型性能

    Args:
        models: 模型字典
        datasets: 数据集字典

    Returns:
        模型的准确率字典
    """
    print("\n" + "=" * 60)
    print("模型准确率评估")
    print("=" * 60)

    results = {}
    for key, model in models.items():
        name = 'RandomForest'
        results[name] = evaluate_model(model, datasets, name)

    return results


# ==================== 模型导出 ====================
def save_verify_data(
    X_verify: np.ndarray,
    y_prob: np.ndarray,
    y_verify: np.ndarray,
    output_dir: str
) -> None:
    """
    保存验证数据

    Args:
        X_verify: 验证特征
        y_prob: 预测概率
        y_verify: 验证标签
        output_dir: 输出目录
    """
    X_verify.tofile(os.path.join(output_dir, "verify_features_tokens.bin"))
    y_prob.tofile(os.path.join(output_dir, "verify_probs_tokens.bin"))
    y_verify.tofile(os.path.join(output_dir, "verify_labels_tokens.bin"))
    print("[OK] 验证数据已保存")


def export_onnx_model(
    models: Dict[str, Any],
    X_test: np.ndarray,
    y_test: np.ndarray,
    output_dir: str
) -> None:
    """
    导出 ONNX 格式模型

    Args:
        models: 模型字典
        X_test: 测试数据（用于验证）
        y_test: 测试标签
        output_dir: 输出目录
    """
    try:
        import onnxmltools
        from onnxmltools.convert import convert_sklearn
        from onnxmltools.convert.common.data_types import FloatTensorType
        import onnxruntime as ort

        print("\n" + "=" * 60)
        print("导出 ONNX 模型...")
        print("=" * 60)

        # 在保存模型前，打印预测数据与原始数据对比
        print("\n" + "=" * 70)
        print("预测数据与原始数据对比 (前20条)")
        print("=" * 70)

        model = models['random_forest']
        y_pred = model.predict(X_test[:20])
        y_true = y_test[:20]

        # 打印表头
        print(f"{'序号':<4} {'原始标签':<8} {'预测标签':<8} {'是否正确':<8} {'对比结果':<20}")
        print("-" * 70)

        for i in range(20):
            is_correct = "✓" if y_pred[i] == y_true[i] else "✗"
            comparison = f"{y_true[i]} vs {y_pred[i]}"
            if y_pred[i] == y_true[i]:
                comparison = f"✓ {y_true[i]}"
            else:
                comparison = f"✗ 期望{y_true[i]} 实际{y_pred[i]}"
            print(f"{i:<4} {y_true[i]:<8} {y_pred[i]:<8} {is_correct:<8} {comparison:<20}")

        print("-" * 70)
        correct_count = sum(y_pred == y_true)
        print(f"前20条准确率: {correct_count}/20 = {correct_count/20:.2%}")
        print("=" * 70)

        # 随机森林 ONNX
        print("\n导出随机森林 ONNX...")
        from skl2onnx import convert_sklearn as skl_convert_sklearn
        from skl2onnx.common.data_types import FloatTensorType as SklearnFloatTensorType

        # 设置 opset 版本为 12（兼容性更好）
        # 注意：使用 opset 21 会导致 IR version 10，某些 ONNX Runtime 版本不支持
        # 使用 opset 12 会让 IR version 保持在 7，兼容性更好
        target_opset = 12
        print(f"使用 opset 版本: {target_opset}")

        initial_type = [('float_input', SklearnFloatTensorType([None, Config.FEATURE_DIM]))]
        # 使用 options 参数禁用 zipmap，输出概率数组格式
        rf_model = models['random_forest']

        onnx_rf = skl_convert_sklearn(
            rf_model,
            initial_types=initial_type,
            target_opset=target_opset,
            options={id(rf_model): {'zipmap': False}}
        )
        onnx_rf_path = os.path.join(output_dir, "model_tokens_rf.onnx")
        # 使用 ONNX 原生的 save 方法，避免版本问题
        import onnx
        onnx.save(onnx_rf, onnx_rf_path)
        print(f"[OK] 随机森林 ONNX 模型已保存: {onnx_rf_path} (opset {target_opset})")

        # 验证 ONNX 模型
        _verify_onnx_model(onnx_rf_path, X_test)

    except ImportError:
        print("\n提示: 安装 onnxmltools 以导出 ONNX 格式")
        print("  pip install onnxmltools onnxruntime")
    except Exception as e:
        print(f"[ERROR] ONNX 导出失败: {e}")


def _verify_onnx_model(
    rf_path: str,
    X_test: np.ndarray
) -> None:
    """
    验证 ONNX 模型输出

    Args:
        rf_path: 随机森林 ONNX 模型路径
        X_test: 测试数据
    """
    try:
        import onnxruntime as ort

        test_input = X_test[:22].astype(np.float32)

        # 随机森林验证
        sess_rf = ort.InferenceSession(rf_path)
        onnx_rf_pred = sess_rf.run(None, {'float_input': test_input})[0]

        print(f"\n[OK] 随机森林 ONNX 预测验证通过")
        print(f"  ONNX RF:     {onnx_rf_pred}")

    except Exception as e:
        print(f"[ERROR] ONNX 验证失败: {e}")


def save_model(
    models: Dict[str, Any],
    datasets: Dict[str, Any],
    output_dir: str
) -> None:
    """
    保存模型和验证数据

    Args:
        models: 模型字典
        datasets: 数据集字典
        output_dir: 输出目录
    """
    print("\n" + "=" * 60)
    print("保存模型文件...")
    print("=" * 60)

    # 保存验证数据
    y_prob = models['random_forest'].predict_proba(datasets['X_verify'])[:, 1]
    # save_verify_data(
    #     datasets['X_verify'],
    #     y_prob,
    #     datasets['y_verify'],
    #     output_dir
    # )

    # 导出 ONNX 模型
    export_onnx_model(models, datasets['X_test'], datasets['y_test'], output_dir)


# ==================== 主流程 ====================
def main():
    """主训练流程"""
    print("Loading data...")

    # 1. 加载数据（使用平衡去重策略）
    # 对于 SQL 注入检测场景：
    # - 正常请求确实很固定和重复（这是正常流量的特征）
    # - 攻击请求有很多变种
    # - 只对攻击样本去重，保留正常样本的重复
    X, y = load_data(Config.DATA_PATH, Config.WINDOW_SIZE, dedup_strategy="balanced")
    print(f"Data shape: X={X.shape}, y={y.shape}")
    print(f"Label distribution:  0={sum(y==0)}, 1={sum(y==1)}")

    # 2. 划分数据集
    datasets = prepare_datasets(
        X, y,
        test_size=Config.TEST_SIZE,
        random_state=Config.RANDOM_STATE,
        verify_size=Config.VERIFY_SIZE
    )

    # 数据诊断
    print("\n" + "=" * 60)
    print("训练数据诊断")
    print("=" * 60)
    print(f"X_train 形状: {datasets['X_train'].shape}")
    print(f"X_train 范围: {datasets['X_train'].min()} ~ {datasets['X_train'].max()}")
    print(f"X_train 均值: {datasets['X_train'].mean():.2f}")
    print(f"X_train 标准差: {datasets['X_train'].std():.2f}")
    print(f"y_train 唯一值: {np.unique(datasets['y_train'])}")
    from collections import Counter
    print(f"y_train 分布: {Counter(datasets['y_train'])}")
    print("=" * 60)

    # 3. 训练模型
    models = train_model(datasets)

    # 4. 评测模型
    results = evaluate_model_performance(models, datasets)
    print_classification_reports(models, datasets)

    # 5. 保存模型
    output_dir = os.path.dirname(os.path.abspath(__file__))
    save_model(models, datasets, output_dir)

    print("\n" + "=" * 60)
    print("训练完成！")
    print("=" * 60)


if __name__ == "__main__":
    main()

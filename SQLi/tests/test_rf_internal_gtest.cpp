/* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved. */
/**
 * @file test_rf_internal_gtest.cpp
 * @brief kadd_rf_engine 内部防御性校验测试。
 */

/* 直接包含实现，以构造绕过文件校验的异常拓扑并访问 kadd_rf_model_t 内部布局。 */
#include "kadd_rf_engine.c"

#include <gtest/gtest.h>

namespace {

/*
 * 构造内部测试模型。必须走本函数而不是在栈上手工赋值：
 *   - SVE 构建的 kadd_rf_model_t 额外含 sve_* 视图，只由 prepare_sve_layout()
 *     填充，缺失时批量路径的首个 gather 会解引用空指针；
 *   - kadd_rf_model_free() 会释放 tree_offsets / nodes / 结构体本身，
 *     故三者均须堆分配。
 */
kadd_rf_model_t *build_model(uint32_t feature_count,
                             uint32_t tree_count,
                             const uint32_t *tree_offsets,
                             uint32_t node_count,
                             const kadd_rf_node_t *nodes)
{
    kadd_rf_model_t *model =
        static_cast<kadd_rf_model_t *>(calloc(1U, sizeof(*model)));
    if (!model) {
        return nullptr;
    }
    model->feature_count = feature_count;
    model->tree_count = tree_count;
    model->node_count = node_count;
    model->tree_offsets = static_cast<uint32_t *>(
        malloc(sizeof(uint32_t) * (static_cast<size_t>(tree_count) + 1U)));
    model->nodes = static_cast<kadd_rf_node_t *>(
        malloc(sizeof(kadd_rf_node_t) * static_cast<size_t>(node_count)));
    if (!model->tree_offsets || !model->nodes) {
        kadd_rf_model_free(&model);
        return nullptr;
    }
    for (uint32_t i = 0; i <= tree_count; ++i) {
        model->tree_offsets[i] = tree_offsets[i];
    }
    for (uint32_t i = 0; i < node_count; ++i) {
        model->nodes[i] = nodes[i];
    }
#if KADD_RF_HAVE_SVE
    if (prepare_sve_layout(model) != KADD_SUCCESS) {
        kadd_rf_model_free(&model);
        return nullptr;
    }
#endif
    return model;
}

/* RAII 句柄：析构时释放模型，并由 kadd_rf_model_free() 置空调用者句柄。 */
class OwnedModel {
public:
    explicit OwnedModel(kadd_rf_model_t *model) : model_(model) {}
    ~OwnedModel() { kadd_rf_model_free(&model_); }
    OwnedModel(const OwnedModel &) = delete;
    OwnedModel &operator=(const OwnedModel &) = delete;

    bool ok() const { return model_ != nullptr; }
    kadd_rf_model_t *get() const { return model_; }

private:
    kadd_rf_model_t *model_;
};

/* 节点构造器，字段序为 feature / left / right / reserved / threshold / probability。 */
kadd_rf_node_t leaf(double probability)
{
    const kadd_rf_node_t node = {-1, UINT32_MAX, UINT32_MAX, 0U, 0.0, probability};
    return node;
}

kadd_rf_node_t split(int32_t feature, double threshold, uint32_t left, uint32_t right)
{
    const kadd_rf_node_t node = {feature, left, right, 0U, threshold, 0.0};
    return node;
}

/*
 * 最小合法模型：feature_count = 2，单棵树 3 节点。
 * node0 按 feature[0] 与 0.5 分流到 node1(p=0.25) / node2(p=0.75)，
 * 故 feature[0] <= 0.5 → 0.25，否则 0.75。
 */
kadd_rf_model_t *build_simple_model()
{
    const uint32_t offsets[2] = {0U, 3U};
    const kadd_rf_node_t nodes[3] = {split(0, 0.5, 1U, 2U), leaf(0.25), leaf(0.75)};
    return build_model(2U, 1U, offsets, 3U, nodes);
}

/* 单节点自环：validate_tree() 会拒绝这种拓扑，此处直接注入以验证推理期防御。 */
kadd_rf_model_t *build_cyclic_model()
{
    const uint32_t offsets[2] = {0U, 1U};
    const kadd_rf_node_t nodes[1] = {split(0, 0.0, 0U, 0U)};
    return build_model(1U, 1U, offsets, 1U, nodes);
}

constexpr int kRows = 3;
constexpr int kDim = 2;
constexpr float kSentinel = -1.0F;

}  // namespace

TEST(RfInternalDefenseTest, PredictRejectsCyclicTreeTopology)
{
    OwnedModel model(build_cyclic_model());
    ASSERT_TRUE(model.ok());

    const float features[1] = {0.0F};
    float probability = kSentinel;
    EXPECT_EQ(kadd_rf_model_predict(model.get(), features, &probability),
              KADD_ERROR_MODEL_LOAD_FAILED);
    EXPECT_EQ(probability, kSentinel) << "拓扑错误时不得写输出";
}

/* 非法值分别落在首个分片、末个分片与首列/次列（batch_size=2 强制多分片）。 */
TEST(RfFeatureValidationTest, BatchRejectsNonFiniteFeatures)
{
    OwnedModel model(build_simple_model());
    ASSERT_TRUE(model.ok());

    const float nan_first_chunk[kRows * kDim] = {NAN, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F};
    const float nan_last_chunk[kRows * kDim] = {0.0F, 0.0F, 1.0F, 0.0F, 0.0F, NAN};
    const float positive_inf[kRows * kDim] = {0.0F, 0.0F, INFINITY, 0.0F, 0.0F, 0.0F};
    const float negative_inf[kRows * kDim] = {0.0F, 0.0F, 1.0F, -INFINITY, 0.0F, 0.0F};
    const float *const cases[] = {nan_first_chunk, nan_last_chunk,
                                  positive_inf, negative_inf};

    for (const float *features : cases) {
        float probabilities[kRows] = {kSentinel, kSentinel, kSentinel};
        EXPECT_EQ(kadd_rf_model_predict_batch(model.get(), features, kRows,
                                              probabilities, 2),
                  KADD_ERROR_INVALID_PARAM);
        for (int i = 0; i < kRows; ++i) {
            EXPECT_EQ(probabilities[i], kSentinel)
                << "校验失败时不得写出任何概率（含已处理的分片）";
        }
    }
}

TEST(RfFeatureValidationTest, BatchAcceptsFiniteFeaturesAndMatchesSingle)
{
    OwnedModel model(build_simple_model());
    ASSERT_TRUE(model.ok());

    const float features[kRows * kDim] = {0.0F, 0.0F, 1.0F, 0.0F, 0.25F, 0.75F};
    const float expected[kRows] = {0.25F, 0.75F, 0.25F};
    float probabilities[kRows] = {kSentinel, kSentinel, kSentinel};

    ASSERT_EQ(kadd_rf_model_predict_batch(model.get(), features, kRows,
                                          probabilities, 2),
              KADD_SUCCESS);
    for (int row = 0; row < kRows; ++row) {
        float single = kSentinel;
        ASSERT_EQ(kadd_rf_model_predict(model.get(), features + row * kDim, &single),
                  KADD_SUCCESS);
        EXPECT_FLOAT_EQ(probabilities[row], single) << "row=" << row;
        EXPECT_FLOAT_EQ(probabilities[row], expected[row]);
    }
}

/* 加载路径已拒绝 feature_count == 0，批量入口的溢出安全判断不得因此除零。 */
TEST(RfInternalDefenseTest, BatchRejectsDegenerateZeroFeatureCount)
{
    const uint32_t offsets[2] = {0U, 1U};
    const kadd_rf_node_t nodes[1] = {leaf(0.5)};
    OwnedModel model(build_model(0U, 1U, offsets, 1U, nodes));
    ASSERT_TRUE(model.ok());

    const float features[1] = {0.0F};
    float probability = kSentinel;
    EXPECT_EQ(kadd_rf_model_predict_batch(model.get(), features, 1, &probability, 1),
              KADD_ERROR_INVALID_PARAM);
    EXPECT_EQ(probability, kSentinel) << "退化模型不得写输出";
}

/* 拓扑错误须由批量接口透传，且不得写出任何概率。 */
TEST(RfInternalDefenseTest, BatchPropagatesCyclicTreeTopologyError)
{
    OwnedModel model(build_cyclic_model());
    ASSERT_TRUE(model.ok());

    const float features[2] = {0.0F, 0.0F};   /* 2 行 x 1 特征，均为有限值 */
    float probabilities[2] = {kSentinel, kSentinel};
    EXPECT_EQ(kadd_rf_model_predict_batch(model.get(), features, 2, probabilities, 1),
              KADD_ERROR_MODEL_LOAD_FAILED);
    EXPECT_EQ(probabilities[0], kSentinel);
    EXPECT_EQ(probabilities[1], kSentinel);
}

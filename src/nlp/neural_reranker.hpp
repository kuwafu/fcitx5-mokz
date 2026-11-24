#pragma once

#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include "ggml.h"
#include "ggml-cpu.h"
#include "ggml-backend.h"

namespace mokz {

struct RerankToken {
    uint16_t wordId; // 0..4095 (0 = UNK)
    uint16_t posId;  // 0..2671 (Mozc lid)
};

class NeuralReranker {
public:
    static constexpr uint32_t MODEL_MAGIC = 0x52555247; // "GRUR"
    static constexpr uint32_t MODEL_VERSION = 1;

    static constexpr int VOCAB_SIZE = 4096;
    static constexpr int POS_SIZE   = 2672;
    static constexpr int DIM_WORD   = 64;
    static constexpr int DIM_POS    = 32;
    static constexpr int DIM_IN     = DIM_WORD + DIM_POS; // 96
    static constexpr int DIM_HIDDEN = 64;

    NeuralReranker() {
        backend_ = ggml_backend_cpu_init();
        initWeightsMemory();
        loadVocab("/usr/share/fcitx5/mokz/reranker_vocab.txt");
        loadModel("/usr/share/fcitx5/mokz/reranker_model.bin");
    }

    ~NeuralReranker() {
        if (weightsCtx_) {
            ggml_free(weightsCtx_);
            weightsCtx_ = nullptr;
        }
        if (backend_) {
            ggml_backend_free(backend_);
            backend_ = nullptr;
        }
    }

    bool isLoaded() const {
        return isLoaded_;
    }

    bool loadVocab(const std::string& vocabPath) {
        std::ifstream ifs(vocabPath);
        if (!ifs) return false;

        vocabMap_.clear();
        std::string line;
        uint16_t id = 1; // 0 is reserved for [UNK]
        while (std::getline(ifs, line) && id < VOCAB_SIZE) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) {
                vocabMap_[line] = id++;
            }
        }
        return true;
    }

    uint16_t getWordId(const std::string& word) const {
        auto it = vocabMap_.find(word);
        return (it != vocabMap_.end()) ? it->second : 0;
    }

    bool loadModel(const std::string& modelPath) {
        if (!weightsCtx_) return false;

        std::ifstream ifs(modelPath, std::ios::binary);
        if (!ifs) {
            isLoaded_ = false;
            return false;
        }

        uint32_t magic = 0, version = 0;
        ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        ifs.read(reinterpret_cast<char*>(&version), sizeof(version));

        if (magic != MODEL_MAGIC || version != MODEL_VERSION) {
            isLoaded_ = false;
            return false;
        }

        struct ggml_tensor* targetTensors[] = {
            embWord_, embPos_,
            w_fwd_z_x_, w_fwd_z_h_, w_fwd_r_x_, w_fwd_r_h_, w_fwd_c_x_, w_fwd_c_h_,
            w_bwd_z_x_, w_bwd_z_h_, w_bwd_r_x_, w_bwd_r_h_, w_bwd_c_x_, w_bwd_c_h_,
            w_out_fwd_, w_out_bwd_
        };

        for (auto* t : targetTensors) {
            size_t nbytes = ggml_nbytes(t);
            ifs.read(reinterpret_cast<char*>(t->data), nbytes);
            if (!ifs) {
                isLoaded_ = false;
                return false;
            }
        }

        isLoaded_ = true;
        return true;
    }

    float evaluate(const std::vector<RerankToken>& tokens) const {
        if (!isLoaded_ || tokens.empty() || !backend_ || !weightsCtx_) {
            return 0.0f;
        }

        int seqLen = static_cast<int>(tokens.size());

        size_t memSize = 4 * 1024 * 1024;
        struct ggml_init_params params = { memSize, nullptr, false };
        struct ggml_context* ctx = ggml_init(params);
        if (!ctx) return 0.0f;

        struct ggml_cgraph* gf = ggml_new_graph(ctx);

        struct ggml_tensor* inp = ggml_new_tensor_2d(ctx, GGML_TYPE_F32, DIM_IN, seqLen);
        float* inpData = reinterpret_cast<float*>(inp->data);

        const float* wWord = reinterpret_cast<const float*>(embWord_->data);
        const float* wPos  = reinterpret_cast<const float*>(embPos_->data);

        for (int t = 0; t < seqLen; ++t) {
            uint16_t wid = (tokens[t].wordId < VOCAB_SIZE) ? tokens[t].wordId : 0;
            uint16_t pid = (tokens[t].posId < POS_SIZE) ? tokens[t].posId : 0;

            float* dstWord = inpData + t * DIM_IN;
            float* dstPos  = dstWord + DIM_WORD;

            std::memcpy(dstWord, wWord + wid * DIM_WORD, sizeof(float) * DIM_WORD);
            std::memcpy(dstPos,  wPos  + pid * DIM_POS,  sizeof(float) * DIM_POS);
        }

        struct ggml_tensor* h_fwd = ggml_new_tensor_1d(ctx, GGML_TYPE_F32, DIM_HIDDEN);
        std::memset(h_fwd->data, 0, sizeof(float) * DIM_HIDDEN);
        for (int t = 0; t < seqLen; ++t) {
            struct ggml_tensor* xt = ggml_view_1d(ctx, inp, DIM_IN, t * DIM_IN * sizeof(float));
            h_fwd = gruCell(ctx, xt, h_fwd, w_fwd_z_x_, w_fwd_z_h_, w_fwd_r_x_, w_fwd_r_h_, w_fwd_c_x_, w_fwd_c_h_);
        }

        struct ggml_tensor* h_bwd = ggml_new_tensor_1d(ctx, GGML_TYPE_F32, DIM_HIDDEN);
        std::memset(h_bwd->data, 0, sizeof(float) * DIM_HIDDEN);
        for (int t = seqLen - 1; t >= 0; --t) {
            struct ggml_tensor* xt = ggml_view_1d(ctx, inp, DIM_IN, t * DIM_IN * sizeof(float));
            h_bwd = gruCell(ctx, xt, h_bwd, w_bwd_z_x_, w_bwd_z_h_, w_bwd_r_x_, w_bwd_r_h_, w_bwd_c_x_, w_bwd_c_h_);
        }

        struct ggml_tensor* out1 = ggml_mul_mat(ctx, w_out_fwd_, h_fwd);
        struct ggml_tensor* out2 = ggml_mul_mat(ctx, w_out_bwd_, h_bwd);
        struct ggml_tensor* score = ggml_add(ctx, out1, out2);

        ggml_build_forward_expand(gf, score);
        enum ggml_status status = ggml_backend_graph_compute(backend_, gf);

        float result = 0.0f;
        if (status == GGML_STATUS_SUCCESS) {
            result = *reinterpret_cast<float*>(score->data);
        }

        ggml_free(ctx);
        return result;
    }

    int evaluateCost(const std::vector<RerankToken>& tokens, float scale = 800.0f) const {
        float s = evaluate(tokens);
        return static_cast<int>(-s * scale);
    }

private:
    static struct ggml_tensor* gruCell(
        struct ggml_context* ctx,
        struct ggml_tensor* x,
        struct ggml_tensor* h_prev,
        struct ggml_tensor* w_zx, struct ggml_tensor* w_zh,
        struct ggml_tensor* w_rx, struct ggml_tensor* w_rh,
        struct ggml_tensor* w_cx, struct ggml_tensor* w_ch)
    {
        auto z = ggml_sigmoid(ctx, ggml_add(ctx, ggml_mul_mat(ctx, w_zx, x), ggml_mul_mat(ctx, w_zh, h_prev)));
        auto r = ggml_sigmoid(ctx, ggml_add(ctx, ggml_mul_mat(ctx, w_rx, x), ggml_mul_mat(ctx, w_rh, h_prev)));
        auto rh = ggml_mul(ctx, r, h_prev);
        auto c = ggml_tanh(ctx, ggml_add(ctx, ggml_mul_mat(ctx, w_cx, x), ggml_mul_mat(ctx, w_ch, rh)));
        auto one_minus_z = ggml_add(ctx, ggml_mul(ctx, z, ggml_new_f32(ctx, -1.0f)), ggml_new_f32(ctx, 1.0f));
        auto part1 = ggml_mul(ctx, one_minus_z, h_prev);
        auto part2 = ggml_mul(ctx, z, c);
        return ggml_add(ctx, part1, part2);
    }

    void initWeightsMemory() {
        size_t memSize = 4 * 1024 * 1024;
        struct ggml_init_params params = { memSize, nullptr, false };
        weightsCtx_ = ggml_init(params);
        if (!weightsCtx_) return;

        embWord_ = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_WORD, VOCAB_SIZE);
        embPos_  = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_POS,  POS_SIZE);

        w_fwd_z_x_ = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_IN, DIM_HIDDEN);
        w_fwd_z_h_ = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_HIDDEN, DIM_HIDDEN);
        w_fwd_r_x_ = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_IN, DIM_HIDDEN);
        w_fwd_r_h_ = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_HIDDEN, DIM_HIDDEN);
        w_fwd_c_x_ = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_IN, DIM_HIDDEN);
        w_fwd_c_h_ = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_HIDDEN, DIM_HIDDEN);

        w_bwd_z_x_ = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_IN, DIM_HIDDEN);
        w_bwd_z_h_ = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_HIDDEN, DIM_HIDDEN);
        w_bwd_r_x_ = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_IN, DIM_HIDDEN);
        w_bwd_r_h_ = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_HIDDEN, DIM_HIDDEN);
        w_bwd_c_x_ = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_IN, DIM_HIDDEN);
        w_bwd_c_h_ = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_HIDDEN, DIM_HIDDEN);

        w_out_fwd_ = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_HIDDEN, 1);
        w_out_bwd_ = ggml_new_tensor_2d(weightsCtx_, GGML_TYPE_F32, DIM_HIDDEN, 1);
    }

    ggml_backend_t backend_ = nullptr;
    struct ggml_context* weightsCtx_ = nullptr;
    bool isLoaded_ = false;
    std::unordered_map<std::string, uint16_t> vocabMap_;

    struct ggml_tensor* embWord_ = nullptr;
    struct ggml_tensor* embPos_  = nullptr;
    struct ggml_tensor* w_fwd_z_x_ = nullptr;
    struct ggml_tensor* w_fwd_z_h_ = nullptr;
    struct ggml_tensor* w_fwd_r_x_ = nullptr;
    struct ggml_tensor* w_fwd_r_h_ = nullptr;
    struct ggml_tensor* w_fwd_c_x_ = nullptr;
    struct ggml_tensor* w_fwd_c_h_ = nullptr;
    struct ggml_tensor* w_bwd_z_x_ = nullptr;
    struct ggml_tensor* w_bwd_z_h_ = nullptr;
    struct ggml_tensor* w_bwd_r_x_ = nullptr;
    struct ggml_tensor* w_bwd_r_h_ = nullptr;
    struct ggml_tensor* w_bwd_c_x_ = nullptr;
    struct ggml_tensor* w_bwd_c_h_ = nullptr;
    struct ggml_tensor* w_out_fwd_ = nullptr;
    struct ggml_tensor* w_out_bwd_ = nullptr;
};

} // namespace mokz

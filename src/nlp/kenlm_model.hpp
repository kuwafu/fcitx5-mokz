#pragma once

#include <string>
#include <vector>
#include <memory>
#include <exception>
#include "lm/model.hh"

namespace mokz {

class KenLMModel {
public:
    KenLMModel(const std::string& path = "/usr/share/fcitx5/mokz/wiki_3gram.binary") {
        try {
            lm::ngram::Config config;
            config.load_method = util::LAZY; // OSのmmapによるオンデマンド高速ロード
            model_.reset(lm::ngram::LoadVirtual(path.c_str(), config));
        } catch (const std::exception&) {
            model_.reset(nullptr);
        }
    }

    bool isLoaded() const {
        return model_ != nullptr;
    }

    // 単語列の 3-gram 対数確率 (log10) の合計を返す（0に近いほど高確率、負の値）
    float scoreLog10(const std::vector<std::string>& words) const {
        if (!model_ || words.empty()) return 0.0f;

        const lm::base::Vocabulary& vocab = model_->BaseVocabulary();
        lm::ngram::State state, out_state;
        model_->BeginSentenceWrite(&state);

        float total_log10 = 0.0f;
        for (const auto& w : words) {
            if (w.empty()) continue;
            lm::WordIndex idx = vocab.Index(w);
            total_log10 += model_->BaseScore(&state, idx, &out_state);
            state = out_state;
        }

        // 文末 </s> への遷移確率も加算
        total_log10 += model_->BaseScore(&state, vocab.EndSentence(), &out_state);
        return total_log10;
    }

    // Mozcコスト体系（値が小さいほど優先）に合わせたペナルティコストへ変換
    int evaluateCost(const std::vector<std::string>& words, float weight = 500.0f) const {
        if (!model_ || words.empty()) return 0;
        float log10_prob = scoreLog10(words);
        // log10_prob は負数のため、マイナスを掛けて正のコスト値にする
        return static_cast<int>(-log10_prob * weight);
    }

private:
    std::unique_ptr<lm::base::Model> model_;
};

} // namespace mokz

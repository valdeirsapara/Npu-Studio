#include "hfstudio/text_engine.hpp"

#include <openvino/genai/chat_history.hpp>
#include <openvino/genai/llm_pipeline.hpp>

#include <chrono>
#include <stdexcept>

namespace fs = std::filesystem;

namespace hfstudio {

struct TextEngine::Impl {
    TextEngineOptions opts;
    std::unique_ptr<ov::genai::LLMPipeline> pipe;
    mutable ov::genai::Tokenizer tokenizer;  // encode() is non-const
    ov::genai::ChatHistory history;
    double load_s = 0;

    Impl(const fs::path& dir, TextEngineOptions o) : opts(std::move(o)) {
        if (!fs::exists(dir / "openvino_model.xml"))
            throw std::runtime_error("modelo de texto nao encontrado em " + dir.string());

        ov::AnyMap props;
        props["CACHE_DIR"] = (dir / ".ov_cache" / opts.device).string();
        if (opts.device == "NPU") {
            props["MAX_PROMPT_LEN"] = static_cast<int64_t>(opts.max_prompt_tokens);
            props["MIN_RESPONSE_LEN"] = static_cast<int64_t>(opts.max_new_tokens);
        }
        auto t0 = std::chrono::steady_clock::now();
        pipe = std::make_unique<ov::genai::LLMPipeline>(dir, opts.device, props);
        load_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        tokenizer = pipe->get_tokenizer();
        reset();
    }

    void reset() {
        history.clear();
        if (!opts.system_prompt.empty())
            history.push_back({{"role", "system"}, {"content", opts.system_prompt}});
    }

    size_t first_turn_index() const { return opts.system_prompt.empty() ? 0 : 1; }

    size_t count_prompt_tokens() const {
        std::string prompt = tokenizer.apply_chat_template(history, /*add_generation_prompt=*/true);
        auto enc = tokenizer.encode(prompt, ov::AnyMap{{"add_special_tokens", false}});
        return enc.input_ids.get_shape()[1];
    }

    // Drops the oldest user/assistant pairs until the prompt fits. Keeps the system prompt
    // and the newest user message.
    size_t trim_to_fit() {
        size_t dropped = 0;
        while (count_prompt_tokens() > opts.max_prompt_tokens) {
            size_t first = first_turn_index();
            if (history.size() <= first + 1)
                throw std::runtime_error("mensagem longa demais: passa de " +
                                         std::to_string(opts.max_prompt_tokens) + " tokens");
            // ChatHistory only supports pop_back, so rebuild without the oldest pair.
            ov::genai::ChatHistory rebuilt;
            for (size_t i = 0; i < history.size(); ++i)
                if (i < first || i >= first + 2) rebuilt.push_back(history[i]);
            history = std::move(rebuilt);
            ++dropped;
        }
        return dropped;
    }
};

TextEngine::TextEngine(const fs::path& model_dir, TextEngineOptions options)
    : impl_(std::make_unique<Impl>(model_dir, std::move(options))) {}

TextEngine::~TextEngine() = default;

double TextEngine::load_seconds() const { return impl_->load_s; }
const TextEngineOptions& TextEngine::options() const { return impl_->opts; }
void TextEngine::reset() { impl_->reset(); }

size_t TextEngine::turns() const {
    return (impl_->history.size() - impl_->first_turn_index()) / 2;
}

size_t TextEngine::history_tokens() const {
    return impl_->history.size() > impl_->first_turn_index() ? impl_->count_prompt_tokens() : 0;
}

std::string TextEngine::send(const std::string& user_message, const TokenFn& on_chunk, GenerationStats* stats) {
    auto& im = *impl_;
    im.history.push_back({{"role", "user"}, {"content", user_message}});

    size_t dropped = 0;
    try {
        dropped = im.trim_to_fit();
    } catch (...) {
        im.history.pop_back();  // leave the history as it was
        throw;
    }

    bool stopped = false;
    auto streamer = [&](std::string chunk) {
        if (on_chunk && !on_chunk(chunk)) {
            stopped = true;
            return ov::genai::StreamingStatus::STOP;
        }
        return ov::genai::StreamingStatus::RUNNING;
    };

    ov::genai::GenerationConfig cfg = im.pipe->get_generation_config();
    cfg.max_new_tokens = im.opts.max_new_tokens;

    ov::genai::DecodedResults res;
    try {
        res = im.pipe->generate(im.history, cfg, streamer);
    } catch (...) {
        im.history.pop_back();
        throw;
    }
    std::string answer = res.texts.empty() ? std::string{} : res.texts[0];
    im.history.push_back({{"role", "assistant"}, {"content", answer}});

    if (stats) {
        auto m = res.perf_metrics;  // getters are non-const
        stats->input_tokens = m.get_num_input_tokens();
        stats->output_tokens = m.get_num_generated_tokens();
        stats->ttft_ms = m.get_ttft().mean;
        stats->tpot_ms = m.get_tpot().mean;
        stats->tokens_per_sec = m.get_throughput().mean;
        stats->dropped_turns = dropped;
        stats->stopped = stopped;
    }
    return answer;
}

}  // namespace hfstudio

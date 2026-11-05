// Spike: download an OpenVINO model from Hugging Face and generate text on NPU/GPU/CPU.
//
//   spike download <repo> <dir>
//   spike run <dir> [device] [prompt]       device: NPU (default), GPU, CPU

#include "hf_download.hpp"

#include <openvino/genai/llm_pipeline.hpp>

#include <chrono>
#include <iomanip>
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace fs = std::filesystem;

static int run(const fs::path& model_dir, const std::string& device, const std::string& prompt) {
    ov::AnyMap props;
    // Compiled-model cache: the first NPU load compiles the model (slow); later loads reuse it.
    props["CACHE_DIR"] = (model_dir / ".ov_cache").string();
    if (device == "NPU") {
        // NPU defaults are 1024 prompt + 128 response tokens; raise for a chat-like budget.
        props["MAX_PROMPT_LEN"] = 2048;
        props["MIN_RESPONSE_LEN"] = 512;
    }

    std::cout << "Carregando " << model_dir.string() << " em " << device << "...\n";
    auto t0 = std::chrono::steady_clock::now();
    ov::genai::LLMPipeline pipe(model_dir, device, props);
    double load_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "Carregado em " << std::fixed << std::setprecision(1) << load_s << " s\n\n";

    ov::genai::GenerationConfig cfg;
    cfg.max_new_tokens = 300;

    std::cout << "> " << prompt << "\n\n";
    auto streamer = [](std::string chunk) {
        std::cout << chunk << std::flush;
        return ov::genai::StreamingStatus::RUNNING;
    };
    ov::genai::DecodedResults res = pipe.generate(prompt, cfg, streamer);

    auto m = res.perf_metrics;  // getters are non-const (they compute lazily)
    std::cout << "\n\n--- " << device << " ---\n" << std::setprecision(2)
              << "Tokens de entrada : " << m.get_num_input_tokens() << "\n"
              << "Tokens gerados    : " << m.get_num_generated_tokens() << "\n"
              << "Carga (s)         : " << load_s << "\n"
              << "TTFT (ms)         : " << m.get_ttft().mean << "\n"
              << "TPOT (ms/token)   : " << m.get_tpot().mean << "\n"
              << "Throughput (tok/s): " << m.get_throughput().mean << "\n";
    return 0;
}

int main(int argc, char** argv) try {
    SetConsoleOutputCP(CP_UTF8);
    if (argc >= 4 && std::string(argv[1]) == "download") {
        hf::download_repo(argv[2], fs::u8path(argv[3]));
        return 0;
    }
    if (argc >= 3 && std::string(argv[1]) == "run") {
        std::string device = argc >= 4 ? argv[3] : "NPU";
        std::string prompt = argc >= 5 ? argv[4]
            : "Explique em 3 frases o que e uma NPU e por que ela economiza energia.";
        return run(fs::u8path(argv[2]), device, prompt);
    }
    std::cerr << "uso:\n  spike download <repo> <dir>\n  spike run <dir> [NPU|GPU|CPU] [prompt]\n";
    return 2;
} catch (const std::exception& e) {
    std::cerr << "\nERRO: " << e.what() << "\n";
    return 1;
}

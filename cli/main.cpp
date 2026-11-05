// hfstudio — command-line front end for the HF Studio core.
// The WinUI app will call the same core classes; this CLI is for testing them.

#include "hfstudio/http.hpp"
#include "hfstudio/hub_client.hpp"
#include "hfstudio/model_manager.hpp"
#include "hfstudio/text_engine.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace hfstudio;
namespace fs = std::filesystem;

namespace {

std::atomic<bool> g_interrupt{false};

BOOL WINAPI on_ctrl(DWORD type) {
    if (type == CTRL_C_EVENT) {
        g_interrupt = true;
        return TRUE;  // handled: let the current operation stop cleanly
    }
    return FALSE;
}

std::string mb(uint64_t bytes) {
    std::ostringstream s;
    double v = bytes / (1024.0 * 1024.0);
    if (v >= 1024) s << std::fixed << std::setprecision(2) << v / 1024 << " GB";
    else s << std::fixed << std::setprecision(1) << v << " MB";
    return s.str();
}

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// Reads one line as UTF-8. Uses ReadConsoleW on a real console so accented input works.
std::optional<std::string> read_line() {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (!GetConsoleMode(in, &mode)) {
        std::string line;
        if (!std::getline(std::cin, line)) return std::nullopt;
        return line;
    }
    std::wstring line;
    wchar_t buf[512];
    for (;;) {
        DWORD got = 0;
        if (!ReadConsoleW(in, buf, 512, &got, nullptr) || got == 0) return std::nullopt;
        line.append(buf, got);
        if (!line.empty() && line.back() == L'\n') break;
    }
    while (!line.empty() && (line.back() == L'\n' || line.back() == L'\r')) line.pop_back();
    return to_utf8(line);
}

std::string env(const char* name) {
    char* v = nullptr;
    size_t len = 0;
    std::string out;
    if (_dupenv_s(&v, &len, name) == 0 && v) out = v;
    free(v);
    return out;
}

struct Args {
    std::vector<std::string> pos;
    std::map<std::string, std::string> opt;
    std::string get(const std::string& k, const std::string& def = {}) const {
        auto it = opt.find(k);
        return it == opt.end() ? def : it->second;
    }
};

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) == 0 && i + 1 < argc) a.opt[s.substr(2)] = argv[++i];
        else a.pos.push_back(s);
    }
    return a;
}

void usage() {
    std::cout <<
        "HF Studio CLI\n\n"
        "  hfstudio search <texto> [--author OpenVINO] [--limit 20]\n"
        "  hfstudio download <repo>          baixa (ou continua) um modelo\n"
        "  hfstudio list                     modelos instalados\n"
        "  hfstudio remove <repo>\n"
        "  hfstudio chat <repo> [--device NPU|GPU|CPU] [--system \"...\"] [--ctx 2048] [--max-new 512]\n\n"
        "Opcoes gerais: --models-dir <pasta> (padrao: %LOCALAPPDATA%\\HFStudio\\models)\n"
        "Token do Hugging Face (modelos restritos): variavel de ambiente HF_TOKEN\n";
}

int cmd_search(const ModelManager& mm, const Args& a) {
    std::string q = a.pos.size() > 1 ? a.pos[1] : "";
    auto results = mm.hub().search(q, std::stoul(a.get("limit", "20")), a.get("author"));
    if (results.empty()) { std::cout << "Nenhum modelo OpenVINO encontrado.\n"; return 0; }
    std::cout << std::left << std::setw(58) << "MODELO" << std::setw(22) << "TIPO" << std::setw(11) << "DOWNLOADS"
              << "NPU?\n";
    for (const auto& m : results) {
        std::cout << std::setw(58) << m.id << std::setw(22) << (m.pipeline_tag.empty() ? "-" : m.pipeline_tag)
                  << std::setw(11) << m.downloads << (m.likely_npu ? "provavel" : "-") << "\n";
    }
    std::cout << "\nNPU? e um palpite pelo nome; a compatibilidade e confirmada depois do download.\n";
    return 0;
}

int cmd_download(ModelManager& mm, const Args& a) {
    if (a.pos.size() < 2) { usage(); return 2; }
    const std::string repo = a.pos[1];
    std::cout << "Baixando " << repo << " (Ctrl+C pausa; rode de novo para continuar)\n";
    size_t last_file = SIZE_MAX;
    auto m = mm.download(repo, [&](const DownloadProgress& p) {
        if (g_interrupt) return false;
        using S = DownloadProgress::Stage;
        if (p.stage == S::Listing) { std::cout << "Listando arquivos...\n"; return true; }
        if (p.stage == S::Done) { std::cout << "\n"; return true; }
        if (p.file_index != last_file) {
            if (last_file != SIZE_MAX) std::cout << "\n";
            last_file = p.file_index;
        }
        double pct = p.bytes_total ? 100.0 * p.bytes_done / p.bytes_total : 0;
        std::cout << "\r[" << p.file_index + 1 << "/" << p.file_count << "] " << std::left << std::setw(32)
                  << p.file.substr(0, 31) << std::right << std::fixed << std::setprecision(1) << std::setw(5) << pct
                  << "%  " << mb(p.bytes_done) << " / " << mb(p.bytes_total) << "  "
                  << (p.stage == S::Verifying ? "verificando...   " : mb((uint64_t)p.bytes_per_sec) + "/s     ")
                  << std::flush;
        return true;
    });
    std::cout << "Instalado em " << m.path.string() << "\n"
              << "Tipo: " << to_string(m.kind) << " | pesos: " << (m.weight_mode.empty() ? "?" : m.weight_mode)
              << " group_size=" << m.group_size << " | NPU: " << (m.npu_compatible ? "sim" : "nao") << "\n";
    return 0;
}

int cmd_list(const ModelManager& mm) {
    auto models = mm.list();
    std::cout << "Pasta: " << mm.root().string() << "\n\n";
    if (models.empty()) std::cout << "Nenhum modelo instalado.\n";
    for (const auto& m : models) {
        std::cout << m.repo << "\n    " << to_string(m.kind) << " | " << mb(m.size_bytes) << " | pesos "
                  << (m.weight_mode.empty() ? "?" : m.weight_mode) << " | NPU: " << (m.npu_compatible ? "sim" : "nao")
                  << " | baixado " << m.downloaded_at << "\n";
    }
    for (const auto& r : mm.pending_downloads())
        std::cout << r << "\n    download incompleto (use: hfstudio download " << r << ")\n";
    return 0;
}

int cmd_remove(ModelManager& mm, const Args& a) {
    if (a.pos.size() < 2) { usage(); return 2; }
    mm.remove(a.pos[1]);
    std::cout << "Removido: " << a.pos[1] << "\n";
    return 0;
}

int cmd_chat(ModelManager& mm, const Args& a) {
    if (a.pos.size() < 2) { usage(); return 2; }
    auto m = mm.find(a.pos[1]);
    if (!m) { std::cerr << a.pos[1] << " nao esta instalado (use: hfstudio download " << a.pos[1] << ")\n"; return 1; }
    if (m->kind != ModelKind::Text) { std::cerr << "Esse modelo nao e de texto.\n"; return 1; }

    TextEngineOptions o;
    o.device = a.get("device", m->npu_compatible ? "NPU" : "GPU");
    o.system_prompt = a.get("system");
    o.max_prompt_tokens = std::stoul(a.get("ctx", "2048"));
    o.max_new_tokens = std::stoul(a.get("max-new", "512"));
    if (o.device == "NPU" && !m->npu_compatible)
        std::cout << "Aviso: este modelo nao esta no formato da NPU (int4_sym por canal); pode falhar.\n";

    std::cout << "Carregando " << m->repo << " em " << o.device << "...\n";
    TextEngine engine(m->path, o);
    std::cout << "Pronto em " << std::fixed << std::setprecision(1) << engine.load_seconds() << " s. "
              << "Comandos: /reset  /stats  /sair   (Ctrl+C interrompe a resposta)\n";

    GenerationStats last{};
    for (;;) {
        std::cout << "\nvoce> " << std::flush;
        g_interrupt = false;
        auto line = read_line();
        if (!line || g_interrupt) break;
        if (line->empty()) continue;
        if (*line == "/sair" || *line == "/exit") break;
        if (*line == "/reset") { engine.reset(); std::cout << "(conversa apagada)\n"; continue; }
        if (*line == "/stats") {
            std::cout << std::setprecision(2) << "ultima resposta: " << last.output_tokens << " tokens, "
                      << last.tokens_per_sec << " tok/s, 1o token em " << last.ttft_ms << " ms\n"
                      << "historico: " << engine.turns() << " trocas, " << engine.history_tokens() << " / "
                      << engine.options().max_prompt_tokens << " tokens\n";
            continue;
        }
        std::cout << "\n" << o.device << "> " << std::flush;
        try {
            engine.send(*line, [](const std::string& chunk) {
                std::cout << chunk << std::flush;
                return !g_interrupt.load();
            }, &last);
        } catch (const std::exception& e) {
            std::cout << "\n[erro] " << e.what() << "\n";
            continue;
        }
        std::cout << "\n";
        if (last.stopped) std::cout << "(interrompido)\n";
        if (last.dropped_turns) std::cout << "(" << last.dropped_turns << " troca(s) antiga(s) esquecida(s) para caber no contexto)\n";
        std::cout << std::setprecision(1) << "[" << last.tokens_per_sec << " tok/s]\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(on_ctrl, TRUE);
    Args a = parse(argc, argv);
    if (a.pos.empty()) { usage(); return 2; }

    fs::path root = a.opt.count("models-dir") ? fs::u8path(a.get("models-dir")) : ModelManager::default_root();
    ModelManager mm(root, HubClient(env("HF_TOKEN")));

    const std::string& cmd = a.pos[0];
    if (cmd == "search") return cmd_search(mm, a);
    if (cmd == "download") return cmd_download(mm, a);
    if (cmd == "list") return cmd_list(mm);
    if (cmd == "remove") return cmd_remove(mm, a);
    if (cmd == "chat") return cmd_chat(mm, a);
    usage();
    return 2;
} catch (const Cancelled&) {
    std::cerr << "\nInterrompido. Rode o mesmo comando para continuar.\n";
    return 130;
} catch (const std::exception& e) {
    std::cerr << "\nERRO: " << e.what() << "\n";
    return 1;
}

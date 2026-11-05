#include "hfstudio/model_manager.hpp"

#include "hfstudio/http.hpp"

#include <nlohmann/json.hpp>
#include <windows.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <regex>
#include <stdexcept>

namespace fs = std::filesystem;
using nlohmann::json;

namespace hfstudio {
namespace {

constexpr const char* kManifest = "hfstudio.json";
constexpr const char* kDownloadsDir = ".downloads";

std::string now_iso() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// Rejects absolute paths and ".." so a malicious repo listing can't write outside the folder.
fs::path safe_relative(const std::string& repo_path) {
    fs::path p = fs::u8path(repo_path);
    if (p.is_absolute() || p.has_root_name() || p.has_root_directory())
        throw std::runtime_error("caminho invalido no repositorio: " + repo_path);
    for (const auto& part : p)
        if (part == "..") throw std::runtime_error("caminho invalido no repositorio: " + repo_path);
    return p;
}

uint64_t folder_size(const fs::path& dir) {
    uint64_t total = 0;
    for (const auto& e : fs::recursive_directory_iterator(dir))
        if (e.is_regular_file()) total += e.file_size();
    return total;
}

// The NNCF weight-compression settings live in the IR's <rt_info>, at the end of the XML.
void read_weight_compression(const fs::path& xml, std::string& mode, int& group_size) {
    std::ifstream in(xml, std::ios::binary);
    if (!in) return;
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    std::streamoff start = size > 256 * 1024 ? size - 256 * 1024 : 0;
    in.seekg(start);
    std::string tail((size_t)(size - start), '\0');
    in.read(tail.data(), tail.size());

    auto wc = tail.find("<weight_compression>");
    if (wc == std::string::npos) return;
    std::string block = tail.substr(wc, tail.find("</weight_compression>", wc) - wc);
    std::smatch m;
    if (std::regex_search(block, m, std::regex(R"re(<mode value="([^"]+)")re"))) mode = m[1];
    if (std::regex_search(block, m, std::regex(R"re(<group_size value="(-?\d+)")re"))) group_size = std::stoi(m[1]);
}

ModelKind detect_kind(const fs::path& dir) {
    if (fs::exists(dir / "model_index.json")) return ModelKind::Image;  // diffusers layout
    if (fs::exists(dir / "openvino_model.xml")) return ModelKind::Text;
    return ModelKind::Unknown;
}

json to_json(const LocalModel& m) {
    return {{"repo", m.repo}, {"revision", m.revision}, {"size_bytes", m.size_bytes},
            {"downloaded_at", m.downloaded_at}, {"kind", to_string(m.kind)},
            {"weight_mode", m.weight_mode}, {"group_size", m.group_size},
            {"npu_compatible", m.npu_compatible}};
}

}  // namespace

const char* to_string(ModelKind k) {
    switch (k) {
        case ModelKind::Text: return "text";
        case ModelKind::Image: return "image";
        default: return "unknown";
    }
}

ModelManager::ModelManager(fs::path root, HubClient hub) : root_(std::move(root)), hub_(std::move(hub)) {
    fs::create_directories(root_);
}

fs::path ModelManager::default_root() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) throw std::runtime_error("LOCALAPPDATA nao definido");
    return fs::path(buf) / "HFStudio" / "models";
}

std::string ModelManager::folder_name(const std::string& repo) {
    std::string out = repo;
    auto slash = out.find('/');
    if (slash != std::string::npos) out.replace(slash, 1, "--");
    // Keep it a single, safe path segment.
    for (char& c : out)
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    return out;
}

std::optional<LocalModel> ModelManager::load_manifest(const fs::path& dir) const {
    std::ifstream in(dir / kManifest);
    if (!in) return std::nullopt;
    try {
        json j = json::parse(in);
        LocalModel m;
        m.repo = j.value("repo", "");
        m.revision = j.value("revision", "");
        m.path = dir;
        m.size_bytes = j.value("size_bytes", 0ull);
        m.downloaded_at = j.value("downloaded_at", "");
        std::string kind = j.value("kind", "unknown");
        m.kind = kind == "text" ? ModelKind::Text : kind == "image" ? ModelKind::Image : ModelKind::Unknown;
        m.weight_mode = j.value("weight_mode", "");
        m.group_size = j.value("group_size", 0);
        m.npu_compatible = j.value("npu_compatible", false);
        return m;
    } catch (const std::exception&) {
        return std::nullopt;  // corrupt manifest: ignore the folder
    }
}

std::vector<LocalModel> ModelManager::list() const {
    std::vector<LocalModel> out;
    for (const auto& e : fs::directory_iterator(root_)) {
        if (!e.is_directory() || e.path().filename() == kDownloadsDir) continue;
        if (auto m = load_manifest(e.path())) out.push_back(std::move(*m));
    }
    return out;
}

std::optional<LocalModel> ModelManager::find(const std::string& repo) const {
    fs::path dir = root_ / fs::u8path(folder_name(repo));
    return fs::exists(dir) ? load_manifest(dir) : std::nullopt;
}

std::vector<std::string> ModelManager::pending_downloads() const {
    std::vector<std::string> out;
    fs::path dl = root_ / kDownloadsDir;
    if (!fs::exists(dl)) return out;
    for (const auto& e : fs::directory_iterator(dl)) {
        std::ifstream in(e.path() / "repo.txt");
        std::string repo;
        if (in && std::getline(in, repo) && !repo.empty()) out.push_back(repo);
    }
    return out;
}

LocalModel ModelManager::download(const std::string& repo, const DownloadProgressFn& on_progress,
                                  const std::string& revision) {
    using clock = std::chrono::steady_clock;
    DownloadProgress p;
    auto report = [&] {
        if (on_progress && !on_progress(p)) throw Cancelled();
    };
    report();

    // Pin the exact commit so every file comes from the same version, even if the
    // repo changes while we download.
    std::string sha = hub_.revision_sha(repo, revision);
    if (sha.empty()) throw std::runtime_error("nao consegui obter a versao de " + repo);
    auto files = hub_.list_files(repo, sha);

    bool has_ir = false;
    for (auto& f : files) {
        safe_relative(f.path);  // validate early, before writing anything
        if (f.path.size() >= 18 && f.path.compare(f.path.size() - 18, 18, "openvino_model.xml") == 0) has_ir = true;
        p.bytes_total += f.size;
    }
    if (!has_ir)
        throw std::runtime_error(repo + " nao e um modelo no formato OpenVINO "
                                        "(conversao de modelos ainda nao suportada)");

    std::string folder = folder_name(repo);
    fs::path staging = root_ / kDownloadsDir / fs::u8path(folder);
    fs::create_directories(staging);
    { std::ofstream(staging / "repo.txt") << repo << "\n"; }

    p.file_count = files.size();
    p.stage = DownloadProgress::Stage::Downloading;
    auto t0 = clock::now();
    uint64_t done_before_files = 0, session_bytes = 0;
    auto last_report = clock::now() - std::chrono::seconds(1);

    for (size_t i = 0; i < files.size(); ++i) {
        const auto& f = files[i];
        fs::path target = staging / safe_relative(f.path);
        p.file = f.path;
        p.file_index = i;
        uint64_t start_size = fs::exists(target) ? fs::file_size(target) : 0;

        http::download_to_file(hub_.file_url(repo, sha, f.path), target, f.size, [&](uint64_t file_done) {
            p.bytes_done = done_before_files + file_done;
            auto now = clock::now();
            if (file_done > start_size) {
                double secs = std::chrono::duration<double>(now - t0).count();
                p.bytes_per_sec = secs > 0 ? (session_bytes + file_done - start_size) / secs : 0;
            }
            if (now - last_report >= std::chrono::milliseconds(250)) {
                last_report = now;
                return !on_progress || on_progress(p);
            }
            return true;
        }, hub_.token());

        if (!f.sha256.empty()) {
            p.stage = DownloadProgress::Stage::Verifying;
            report();
            if (http::sha256_file(target) != f.sha256) {
                fs::remove(target);
                throw std::runtime_error("arquivo corrompido (SHA-256 nao confere): " + f.path +
                                         " - tente baixar de novo");
            }
            p.stage = DownloadProgress::Stage::Downloading;
        }
        session_bytes += f.size - start_size;
        done_before_files += f.size;
        p.bytes_done = done_before_files;
    }

    // Build the manifest, then move the finished folder into the library.
    LocalModel m;
    m.repo = repo;
    m.revision = sha;
    m.downloaded_at = now_iso();
    m.kind = detect_kind(staging);
    if (m.kind == ModelKind::Text) {
        read_weight_compression(staging / "openvino_model.xml", m.weight_mode, m.group_size);
        m.npu_compatible = m.weight_mode == "int4_sym" && m.group_size == -1;
    }
    fs::remove(staging / "repo.txt");
    m.size_bytes = folder_size(staging);
    { std::ofstream(staging / kManifest) << to_json(m).dump(2); }

    fs::path final_dir = root_ / fs::u8path(folder);
    if (fs::exists(final_dir)) fs::remove_all(final_dir);  // replacing an older version
    fs::rename(staging, final_dir);
    m.path = final_dir;

    p.stage = DownloadProgress::Stage::Done;
    p.bytes_done = p.bytes_total;
    if (on_progress) on_progress(p);
    return m;
}

void ModelManager::remove(const std::string& repo) {
    std::string folder = folder_name(repo);
    bool any = false;
    for (fs::path dir : {root_ / fs::u8path(folder), root_ / kDownloadsDir / fs::u8path(folder)}) {
        if (fs::exists(dir)) { fs::remove_all(dir); any = true; }
    }
    if (!any) throw std::runtime_error(repo + " nao esta instalado");
}

}  // namespace hfstudio

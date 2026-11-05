#pragma once
#include "hfstudio/hub_client.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hfstudio {

enum class ModelKind { Text, Image, Unknown };

struct LocalModel {
    std::string repo;            // "OpenVINO/Phi-3.5-mini-instruct-int4-cw-ov"
    std::string revision;        // commit sha downloaded
    std::filesystem::path path;  // folder with openvino_model.xml etc.
    uint64_t size_bytes = 0;
    std::string downloaded_at;   // ISO-8601 UTC
    ModelKind kind = ModelKind::Unknown;
    std::string weight_mode;     // e.g. "int4_sym", "int4_asym", "int8_asym" ("" if unknown)
    int group_size = 0;          // -1 = channel-wise
    bool npu_compatible = false; // int4_sym + channel-wise (per OpenVINO NPU docs)
};

struct DownloadProgress {
    std::string file;
    size_t file_index = 0, file_count = 0;
    uint64_t bytes_done = 0, bytes_total = 0;  // whole repo
    double bytes_per_sec = 0;
    enum class Stage { Listing, Downloading, Verifying, Done } stage = Stage::Listing;
};

// Return false to cancel.
using DownloadProgressFn = std::function<bool(const DownloadProgress&)>;

// Owns the local model library: <root>/<owner>--<name>/ with an hfstudio.json manifest.
// Downloads go to <root>/.downloads/<folder>/ and are moved into place only when complete
// and verified, so a half-downloaded model never shows up in list().
class ModelManager {
public:
    explicit ModelManager(std::filesystem::path root, HubClient hub = HubClient{});

    // %LOCALAPPDATA%\HFStudio\models
    static std::filesystem::path default_root();

    const std::filesystem::path& root() const { return root_; }
    const HubClient& hub() const { return hub_; }

    std::vector<LocalModel> list() const;
    std::optional<LocalModel> find(const std::string& repo) const;

    // Downloads (or resumes) a repo. Throws Cancelled if on_progress returns false;
    // a later call resumes where it stopped.
    LocalModel download(const std::string& repo, const DownloadProgressFn& on_progress = {},
                        const std::string& revision = "main");

    void remove(const std::string& repo);

    // Partially downloaded repos (can be resumed with download()).
    std::vector<std::string> pending_downloads() const;

    static std::string folder_name(const std::string& repo);  // "owner/name" -> "owner--name"

private:
    std::optional<LocalModel> load_manifest(const std::filesystem::path& dir) const;

    std::filesystem::path root_;
    HubClient hub_;
};

const char* to_string(ModelKind k);

}  // namespace hfstudio

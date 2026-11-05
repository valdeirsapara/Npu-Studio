#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hfstudio {

struct ModelSummary {
    std::string id;            // "OpenVINO/Phi-3.5-mini-instruct-int4-cw-ov"
    uint64_t downloads = 0;
    uint64_t likes = 0;
    std::string pipeline_tag;  // "text-generation", "text-to-image", ...
    std::vector<std::string> tags;
    bool likely_npu = false;   // name/tag hint only; confirmed after download
};

struct RepoFile {
    std::string path;    // relative path inside the repo, '/' separated
    uint64_t size = 0;
    std::string sha256;  // LFS files only; empty otherwise
};

// Hugging Face Hub REST client (https://huggingface.co/api/...).
class HubClient {
public:
    explicit HubClient(std::string token = {});

    // Searches OpenVINO-format models (tag "openvino"), most downloaded first.
    std::vector<ModelSummary> search(const std::string& query, size_t limit = 20,
                                     const std::string& author = {}) const;

    // Commit sha of the revision (pins a download to an exact version).
    std::string revision_sha(const std::string& repo, const std::string& revision = "main") const;

    std::vector<RepoFile> list_files(const std::string& repo, const std::string& revision) const;

    std::string file_url(const std::string& repo, const std::string& revision, const std::string& path) const;

    const std::string& token() const { return token_; }

    // Guess from name/tags whether a repo is NPU-friendly (int4 symmetric channel-wise).
    static bool looks_npu_friendly(const std::string& id, const std::vector<std::string>& tags);

private:
    std::string token_;
};

}  // namespace hfstudio

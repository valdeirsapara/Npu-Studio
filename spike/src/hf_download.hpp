#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hf {

struct RepoFile {
    std::string path;
    uint64_t size = 0;
    std::string sha256;  // only for LFS files; empty otherwise
};

// Lists every file in a model repo via GET /api/models/{repo}/tree/{revision}?recursive=true
std::vector<RepoFile> list_files(const std::string& repo, const std::string& revision = "main");

// Downloads all files of a repo into dest_dir. Resumes partial files (*.part) with
// HTTP Range, skips files already complete, and checks SHA-256 of LFS files.
void download_repo(const std::string& repo, const std::filesystem::path& dest_dir,
                   const std::string& revision = "main");

}  // namespace hf

#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>

namespace hfstudio {

// Thrown when the caller cancels a long operation (download, generation).
struct Cancelled : std::runtime_error {
    Cancelled() : std::runtime_error("cancelado") {}
};

struct HttpError : std::runtime_error {
    int status;
    HttpError(int s, const std::string& what) : std::runtime_error(what), status(s) {}
};

namespace http {

// GET a URL and return the body as text. Throws HttpError on non-2xx.
// `bearer` (optional) is sent as "Authorization: Bearer ..." (HF token for gated repos).
std::string get_text(const std::string& url, const std::string& bearer = {});

// Called while downloading with the total bytes written to `target` so far.
// Return false to cancel (throws Cancelled; the partial file is kept for resume).
using ProgressFn = std::function<bool(uint64_t bytes_done)>;

// Downloads `url` to `target`, resuming from the existing file size with an HTTP Range
// request. Restarts from zero if the server ignores Range.
void download_to_file(const std::string& url, const std::filesystem::path& target,
                      uint64_t expected_size, const ProgressFn& on_progress,
                      const std::string& bearer = {});

// Percent-encodes a string for use in a URL query or path segment.
std::string url_encode(const std::string& s);

// Lowercase hex SHA-256 of a file.
std::string sha256_file(const std::filesystem::path& p);

}  // namespace http
}  // namespace hfstudio

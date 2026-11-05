#include "hf_download.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace hf {
namespace {

constexpr const char* kHost = "https://huggingface.co";
constexpr const wchar_t* kUserAgent = L"hf-studio-spike/0.1";

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error(what + " (win32 error " + std::to_string(GetLastError()) + ")");
}

// Small RAII wrapper for WinHTTP handles.
struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET x) : h(x) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    operator HINTERNET() const { return h; }
};

HINTERNET session() {
    static Handle s(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!s) fail("WinHttpOpen");
    return s;
}

struct Response {
    Handle connect;
    Handle request;
    DWORD status = 0;
};

// Opens a GET request (redirects are followed automatically, e.g. to the LFS CDN).
std::unique_ptr<Response> http_get(const std::string& url, uint64_t range_from = 0) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256], path[4096];
    uc.lpszHostName = host;  uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;   uc.dwUrlPathLength = 4096;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) fail("WinHttpCrackUrl " + url);

    auto r = std::make_unique<Response>();
    r->connect.h = WinHttpConnect(session(), host, uc.nPort, 0);
    if (!r->connect) fail("WinHttpConnect");
    r->request.h = WinHttpOpenRequest(r->connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!r->request) fail("WinHttpOpenRequest");

    std::wstring headers;
    if (range_from > 0) headers = L"Range: bytes=" + std::to_wstring(range_from) + L"-\r\n";
    if (!WinHttpSendRequest(r->request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        fail("WinHttpSendRequest " + url);
    if (!WinHttpReceiveResponse(r->request, nullptr)) fail("WinHttpReceiveResponse " + url);

    DWORD size = sizeof(r->status);
    WinHttpQueryHeaders(r->request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &r->status, &size, WINHTTP_NO_HEADER_INDEX);
    return r;
}

template <typename Sink>
void read_body(HINTERNET req, Sink&& sink) {
    std::vector<char> buf(1 << 20);
    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(req, buf.data(), (DWORD)buf.size(), &got)) fail("WinHttpReadData");
        if (got == 0) break;
        sink(buf.data(), got);
    }
}

std::string sha256_file(const fs::path& p) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0);
    std::ifstream in(p, std::ios::binary);
    std::vector<char> buf(1 << 20);
    while (in) {
        in.read(buf.data(), buf.size());
        if (in.gcount() > 0) BCryptHashData(hash, (PUCHAR)buf.data(), (ULONG)in.gcount(), 0);
    }
    unsigned char digest[32];
    BCryptFinishHash(hash, digest, sizeof(digest), 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    std::ostringstream hex;
    for (unsigned char b : digest) hex << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    return hex.str();
}

std::string human(uint64_t bytes) {
    std::ostringstream s;
    s << std::fixed << std::setprecision(1) << bytes / (1024.0 * 1024.0) << " MB";
    return s.str();
}

void download_file(const std::string& url, const RepoFile& f, const fs::path& target) {
    fs::create_directories(target.parent_path());
    fs::path part = target;
    part += ".part";

    uint64_t have = fs::exists(part) ? fs::file_size(part) : 0;
    if (have > f.size) { fs::remove(part); have = 0; }

    auto r = http_get(url, have);
    std::ios::openmode mode = std::ios::binary;
    if (have > 0 && r->status == 206) {
        mode |= std::ios::app;
        std::cout << "  resumindo de " << human(have) << "\n";
    } else if (r->status == 200 || r->status == 206) {
        have = 0;  // server ignored the Range header: start over
    } else {
        throw std::runtime_error("HTTP " + std::to_string(r->status) + " em " + url);
    }

    std::ofstream out(part, mode);
    if (!out) throw std::runtime_error("nao consegui abrir " + part.string());

    auto start = std::chrono::steady_clock::now();
    auto last_print = start;
    uint64_t done = have, this_session = 0;
    read_body(r->request, [&](const char* data, DWORD n) {
        out.write(data, n);
        done += n;
        this_session += n;
        auto now = std::chrono::steady_clock::now();
        if (now - last_print > std::chrono::milliseconds(500) || done == f.size) {
            double secs = std::chrono::duration<double>(now - start).count();
            double mbps = secs > 0 ? this_session / secs / (1024.0 * 1024.0) : 0;
            std::cout << "\r  " << human(done) << " / " << human(f.size) << "  ("
                      << std::fixed << std::setprecision(1) << mbps << " MB/s)   " << std::flush;
            last_print = now;
        }
    });
    out.close();
    std::cout << "\n";

    if (fs::file_size(part) != f.size)
        throw std::runtime_error("tamanho errado em " + f.path + " (download incompleto, rode de novo para resumir)");
    if (!f.sha256.empty()) {
        std::string got = sha256_file(part);
        if (got != f.sha256) {
            fs::remove(part);
            throw std::runtime_error("SHA-256 nao confere em " + f.path);
        }
        std::cout << "  sha256 ok\n";
    }
    fs::rename(part, target);
}

}  // namespace

std::vector<RepoFile> list_files(const std::string& repo, const std::string& revision) {
    std::string url = std::string(kHost) + "/api/models/" + repo + "/tree/" + revision + "?recursive=true";
    auto r = http_get(url);
    if (r->status != 200) throw std::runtime_error("HTTP " + std::to_string(r->status) + " listando " + repo);
    std::string body;
    read_body(r->request, [&](const char* d, DWORD n) { body.append(d, n); });

    // The tree API returns a flat JSON array of entries. Each entry starts with "type";
    // split on that and pull fields with regexes (enough for a spike, no JSON lib needed).
    std::vector<RepoFile> files;
    static const std::regex entry_re(R"re(\{"type":"(file|directory)")re");
    static const std::regex path_re(R"re("path":"([^"]+)")re");
    static const std::regex size_re(R"re("size":(\d+))re");
    static const std::regex lfs_re(R"re("lfs":\{"oid":"([0-9a-f]{64})")re");

    std::vector<size_t> starts;
    for (auto it = std::sregex_iterator(body.begin(), body.end(), entry_re); it != std::sregex_iterator(); ++it)
        starts.push_back(it->position());
    starts.push_back(body.size());

    for (size_t i = 0; i + 1 < starts.size(); ++i) {
        std::string e = body.substr(starts[i], starts[i + 1] - starts[i]);
        if (e.rfind(R"({"type":"file")", 0) != 0) continue;
        std::smatch m;
        RepoFile f;
        if (std::regex_search(e, m, path_re)) f.path = m[1];
        if (std::regex_search(e, m, size_re)) f.size = std::stoull(m[1]);
        if (std::regex_search(e, m, lfs_re)) f.sha256 = m[1];
        if (!f.path.empty()) files.push_back(f);
    }
    return files;
}

void download_repo(const std::string& repo, const fs::path& dest_dir, const std::string& revision) {
    auto files = list_files(repo, revision);
    uint64_t total = 0;
    for (auto& f : files) total += f.size;
    std::cout << repo << ": " << files.size() << " arquivos, " << human(total) << "\n";

    for (auto& f : files) {
        fs::path target = dest_dir / fs::u8path(f.path);
        if (fs::exists(target) && fs::file_size(target) == f.size) {
            std::cout << "[ok] " << f.path << "\n";
            continue;
        }
        std::cout << "[baixando] " << f.path << " (" << human(f.size) << ")\n";
        download_file(std::string(kHost) + "/" + repo + "/resolve/" + revision + "/" + f.path, f, target);
    }
    std::cout << "Download completo em " << dest_dir.string() << "\n";
}

}  // namespace hf

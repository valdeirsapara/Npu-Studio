#include "hfstudio/http.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace hfstudio::http {
namespace {

constexpr const wchar_t* kUserAgent = L"hf-studio/0.1";

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error(what + " (erro win32 " + std::to_string(GetLastError()) + ")");
}

struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    operator HINTERNET() const { return h; }
};

HINTERNET session() {
    // One session for the whole process (thread-safe init; WinHTTP sessions are shareable).
    static Handle s;
    static const bool ok = [] {
        s.h = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
        return s.h != nullptr;
    }();
    if (!ok) fail("WinHttpOpen");
    return s;
}

struct Response {
    Handle connect;
    Handle request;
    DWORD status = 0;
};

// Opens a GET request. WinHTTP follows redirects (e.g. to the LFS CDN) automatically.
std::unique_ptr<Response> open_get(const std::string& url, const std::string& bearer, uint64_t range_from) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    std::wstring host(256, L'\0'), path(8192, L'\0');
    uc.lpszHostName = host.data();  uc.dwHostNameLength = (DWORD)host.size();
    uc.lpszUrlPath = path.data();   uc.dwUrlPathLength = (DWORD)path.size();
    uc.dwExtraInfoLength = 1;  // ask for the query string too
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) fail("URL invalida: " + url);
    std::wstring full_path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo) full_path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    host.resize(uc.dwHostNameLength);

    auto r = std::make_unique<Response>();
    r->connect.h = WinHttpConnect(session(), host.c_str(), uc.nPort, 0);
    if (!r->connect) fail("WinHttpConnect " + url);
    r->request.h = WinHttpOpenRequest(r->connect, L"GET", full_path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!r->request) fail("WinHttpOpenRequest " + url);

    std::wstring headers;
    if (!bearer.empty()) headers += L"Authorization: Bearer " + widen(bearer) + L"\r\n";
    if (range_from > 0) headers += L"Range: bytes=" + std::to_wstring(range_from) + L"-\r\n";
    if (!WinHttpSendRequest(r->request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                            (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        fail("sem conexao com " + url);
    if (!WinHttpReceiveResponse(r->request, nullptr)) fail("sem resposta de " + url);

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
        if (!WinHttpReadData(req, buf.data(), (DWORD)buf.size(), &got)) fail("conexao interrompida");
        if (got == 0) break;
        sink(buf.data(), got);
    }
}

std::string status_message(DWORD status, const std::string& url) {
    switch (status) {
        case 401: return "acesso negado (401): repositorio privado ou requer token do Hugging Face";
        case 403: return "acesso negado (403): aceite os termos do modelo no site do Hugging Face";
        case 404: return "nao encontrado (404): " + url;
        default:  return "HTTP " + std::to_string(status) + " em " + url;
    }
}

}  // namespace

std::string get_text(const std::string& url, const std::string& bearer) {
    auto r = open_get(url, bearer, 0);
    if (r->status < 200 || r->status >= 300) throw HttpError((int)r->status, status_message(r->status, url));
    std::string body;
    read_body(r->request, [&](const char* d, DWORD n) { body.append(d, n); });
    return body;
}

void download_to_file(const std::string& url, const fs::path& target, uint64_t expected_size,
                      const ProgressFn& on_progress, const std::string& bearer) {
    fs::create_directories(target.parent_path());
    uint64_t have = fs::exists(target) ? fs::file_size(target) : 0;
    if (have > expected_size) { fs::remove(target); have = 0; }
    if (have == expected_size && expected_size > 0) return;

    auto r = open_get(url, bearer, have);
    std::ios::openmode mode = std::ios::binary;
    if (have > 0 && r->status == 206) {
        mode |= std::ios::app;
    } else if (r->status == 200 || r->status == 206) {
        have = 0;  // server ignored Range: start over
    } else {
        throw HttpError((int)r->status, status_message(r->status, url));
    }

    std::ofstream out(target, mode);
    if (!out) throw std::runtime_error("nao consegui gravar " + target.string());
    uint64_t done = have;
    read_body(r->request, [&](const char* data, DWORD n) {
        out.write(data, n);
        done += n;
        if (on_progress && !on_progress(done)) {
            out.close();
            throw Cancelled();
        }
    });
    out.close();
    if (fs::file_size(target) != expected_size)
        throw std::runtime_error("download incompleto de " + target.filename().string() +
                                 " (tente de novo para continuar de onde parou)");
}

std::string url_encode(const std::string& s) {
    std::ostringstream o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o << c;
        else o << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    }
    return o.str();
}

std::string sha256_file(const fs::path& p) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) fail("BCrypt");
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

}  // namespace hfstudio::http

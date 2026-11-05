#include "hfstudio/hub_client.hpp"

#include "hfstudio/http.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

using nlohmann::json;

namespace hfstudio {
namespace {

constexpr const char* kHub = "https://huggingface.co";

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// Repo ids are "owner/name"; encode each part but keep the slash.
std::string encode_repo(const std::string& repo) {
    auto slash = repo.find('/');
    if (slash == std::string::npos) return http::url_encode(repo);
    return http::url_encode(repo.substr(0, slash)) + "/" + http::url_encode(repo.substr(slash + 1));
}

std::string encode_path(const std::string& path) {
    std::string out, part;
    for (char c : path) {
        if (c == '/') { out += http::url_encode(part) + "/"; part.clear(); }
        else part += c;
    }
    return out + http::url_encode(part);
}

}  // namespace

HubClient::HubClient(std::string token) : token_(std::move(token)) {}

bool HubClient::looks_npu_friendly(const std::string& id, const std::vector<std::string>& tags) {
    std::string n = lower(id);
    if (n.find("-cw-") != std::string::npos || n.find("int4-cw") != std::string::npos ||
        n.find("npu") != std::string::npos)
        return true;
    return std::any_of(tags.begin(), tags.end(), [](const std::string& t) { return lower(t) == "npu"; });
}

std::vector<ModelSummary> HubClient::search(const std::string& query, size_t limit, const std::string& author) const {
    std::string url = std::string(kHub) + "/api/models?filter=openvino&sort=downloads&direction=-1&limit=" +
                      std::to_string(limit);
    if (!query.empty()) url += "&search=" + http::url_encode(query);
    if (!author.empty()) url += "&author=" + http::url_encode(author);

    std::vector<ModelSummary> out;
    for (const auto& m : json::parse(http::get_text(url, token_))) {
        ModelSummary s;
        s.id = m.value("id", "");
        s.downloads = m.value("downloads", 0ull);
        s.likes = m.value("likes", 0ull);
        s.pipeline_tag = m.value("pipeline_tag", "");
        if (m.contains("tags")) s.tags = m["tags"].get<std::vector<std::string>>();
        s.likely_npu = looks_npu_friendly(s.id, s.tags);
        if (!s.id.empty()) out.push_back(std::move(s));
    }
    return out;
}

std::string HubClient::revision_sha(const std::string& repo, const std::string& revision) const {
    auto j = json::parse(http::get_text(std::string(kHub) + "/api/models/" + encode_repo(repo) +
                                        "/revision/" + http::url_encode(revision), token_));
    return j.value("sha", "");
}

std::vector<RepoFile> HubClient::list_files(const std::string& repo, const std::string& revision) const {
    std::string url = std::string(kHub) + "/api/models/" + encode_repo(repo) + "/tree/" +
                      http::url_encode(revision) + "?recursive=true";
    std::vector<RepoFile> files;
    for (const auto& e : json::parse(http::get_text(url, token_))) {
        if (e.value("type", "") != "file") continue;
        RepoFile f;
        f.path = e.value("path", "");
        f.size = e.value("size", 0ull);
        if (e.contains("lfs") && e["lfs"].is_object()) f.sha256 = e["lfs"].value("oid", "");
        files.push_back(std::move(f));
    }
    return files;
}

std::string HubClient::file_url(const std::string& repo, const std::string& revision, const std::string& path) const {
    return std::string(kHub) + "/" + encode_repo(repo) + "/resolve/" + http::url_encode(revision) + "/" +
           encode_path(path);
}

}  // namespace hfstudio

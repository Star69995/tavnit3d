#include "update_check.hpp"
#include "branding.hpp"
#include "nlohmann/json.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace dune3d {

std::string get_update_feed_url()
{
    if (auto env = getenv("DUNE3D_UPDATE_FEED"); env && *env)
        return env;
    if (std::string(update_repo).empty())
        return "";
    return std::string("https://api.github.com/repos/") + update_repo + "/releases/latest";
}

static std::vector<long> version_parts(std::string v)
{
    if (v.size() && (v.front() == 'v' || v.front() == 'V'))
        v.erase(0, 1);
    std::vector<long> parts;
    std::istringstream iss(v);
    std::string part;
    while (std::getline(iss, part, '.')) {
        try {
            parts.push_back(std::stol(part));
        }
        catch (...) {
            parts.push_back(0);
        }
    }
    return parts;
}

int compare_versions(const std::string &a, const std::string &b)
{
    auto pa = version_parts(a), pb = version_parts(b);
    const auto n = std::max(pa.size(), pb.size());
    pa.resize(n, 0);
    pb.resize(n, 0);
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i])
            return pa[i] < pb[i] ? -1 : 1;
    }
    return 0;
}

std::optional<UpdateInfo> parse_release(const std::string &json_text, const std::string &current)
{
    const auto j = nlohmann::json::parse(json_text);
    if (j.value("draft", false) || j.value("prerelease", false))
        return {};
    UpdateInfo info;
    info.version = j.value("tag_name", "");
    if (info.version.size() && (info.version.front() == 'v' || info.version.front() == 'V'))
        info.version.erase(0, 1);
    if (info.version.empty() || compare_versions(info.version, current) <= 0)
        return {};
    info.page_url = j.value("html_url", "");
    info.notes = j.value("body", "");
    if (j.contains("assets")) {
        for (const auto &asset : j.at("assets")) {
            const auto name = asset.value("name", "");
            if (name.ends_with("-setup.exe")) {
                info.installer_url = asset.value("browser_download_url", "");
                break;
            }
        }
    }
    if (info.installer_url.empty())
        return {};
    return info;
}

#ifdef _WIN32
namespace {
std::wstring widen(const std::string &s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

class Handle {
public:
    explicit Handle(HINTERNET h) : m_h(h)
    {
        if (!h)
            throw std::runtime_error("network error " + std::to_string(GetLastError()));
    }
    ~Handle()
    {
        WinHttpCloseHandle(m_h);
    }
    operator HINTERNET() const
    {
        return m_h;
    }

private:
    HINTERNET m_h;
};

// GET url, feeding the body to sink; follows redirects (GitHub assets redirect to a CDN)
void http_get(const std::string &url, const std::function<void(const char *, size_t, uint64_t total)> &sink)
{
    const auto wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256], path[2048];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
        throw std::runtime_error("bad URL " + url);

    Handle session{WinHttpOpen(L"Tavnit3D-updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    Handle connect{WinHttpConnect(session, host, uc.nPort, 0)};
    const DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle request{WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
    // GitHub's API wants a user agent (set above) and likes this accept header
    WinHttpAddRequestHeaders(request, L"Accept: application/vnd.github+json, */*", (DWORD)-1L,
                             WINHTTP_ADDREQ_FLAG_ADD);
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(request, nullptr))
        throw std::runtime_error("couldn't reach " + url);

    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200)
        throw std::runtime_error("server answered " + std::to_string(status) + " for " + url);
    uint64_t total = 0;
    {
        wchar_t buf[32];
        DWORD len = sizeof(buf);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, buf, &len,
                                WINHTTP_NO_HEADER_INDEX))
            total = std::wcstoull(buf, nullptr, 10);
    }
    std::vector<char> buf(64 * 1024);
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buf.data(), (DWORD)buf.size(), &read))
            throw std::runtime_error("download interrupted");
        if (read == 0)
            break;
        sink(buf.data(), read, total);
    }
}
} // namespace

std::optional<UpdateInfo> check_for_update(const std::string &feed_url, const std::string &current)
{
    std::string body;
    http_get(feed_url, [&body](const char *d, size_t n, uint64_t) { body.append(d, n); });
    return parse_release(body, current);
}

void download_file(const std::string &url, const std::filesystem::path &dest,
                   const std::function<void(uint64_t, uint64_t)> &progress)
{
    const auto tmp = std::filesystem::path(dest).concat(".part");
    {
        std::ofstream ofs(tmp, std::ios::binary);
        uint64_t done = 0;
        http_get(url, [&](const char *d, size_t n, uint64_t total) {
            ofs.write(d, n);
            done += n;
            if (progress)
                progress(done, total);
        });
        if (!ofs)
            throw std::runtime_error("couldn't write " + tmp.string());
    }
    std::filesystem::rename(tmp, dest);
}

bool launch_installer(const std::filesystem::path &installer)
{
    std::wstring cmd = L"\"" + installer.wstring() + L"\" /SILENT /SUPPRESSMSGBOXES /NORESTART /RELAUNCH=1";
    // the restarted app shouldn't inherit an automation script
    SetEnvironmentVariableW(L"DUNE3D_AUTOMATION", nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
#else
std::optional<UpdateInfo> check_for_update(const std::string &, const std::string &)
{
    return {};
}
void download_file(const std::string &, const std::filesystem::path &, const std::function<void(uint64_t, uint64_t)> &)
{
    throw std::runtime_error("updates are only available on Windows");
}
bool launch_installer(const std::filesystem::path &)
{
    return false;
}
#endif

} // namespace dune3d

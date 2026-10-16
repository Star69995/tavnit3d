#include "http.hpp"
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#endif

namespace dune3d {

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
    Handle(Handle &&other) noexcept : m_h(other.m_h)
    {
        other.m_h = nullptr;
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    ~Handle()
    {
        if (m_h)
            WinHttpCloseHandle(m_h);
    }
    operator HINTERNET() const
    {
        return m_h;
    }

private:
    HINTERNET m_h;
};

struct Request {
    Handle session, connect, request;
};

// GitHub assets redirect to a CDN: WinHTTP follows redirects by itself
std::unique_ptr<Request> open_request(const std::string &url, const wchar_t *method, const HttpHeaders &headers)
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

    // some servers (GitHub's API) want a user agent
    Handle session{WinHttpOpen(L"Tavnit3D", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    Handle connect{WinHttpConnect(session, host, uc.nPort, 0)};
    const DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle request{WinHttpOpenRequest(connect, method, path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      flags)};
    for (const auto &[k, v] : headers) {
        const auto line = widen(k + ": " + v);
        WinHttpAddRequestHeaders(request, line.c_str(), (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    return std::make_unique<Request>(std::move(session), std::move(connect), std::move(request));
}

DWORD get_status(HINTERNET request)
{
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    return status;
}
} // namespace

void http_get(const std::string &url, const std::function<void(const char *, size_t, uint64_t total)> &sink,
              const HttpHeaders &headers)
{
    auto r = open_request(url, L"GET", headers);
    auto &request = r->request;
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(request, nullptr))
        throw std::runtime_error("couldn't reach " + url);

    const auto status = get_status(request);
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


HttpResponse http_post(const std::string &url, const HttpHeaders &headers, const std::string &body, int timeout_s)
{
    auto r = open_request(url, L"POST", headers);
    auto &request = r->request;
    const int ms = timeout_s * 1000;
    WinHttpSetTimeouts(request, 30000, 30000, 60000, ms);
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, const_cast<char *>(body.data()),
                            (DWORD)body.size(), (DWORD)body.size(), 0)
        || !WinHttpReceiveResponse(request, nullptr)) {
        const auto err = GetLastError();
        throw std::runtime_error(err == ERROR_WINHTTP_TIMEOUT ? "no answer in time" : "couldn't reach " + url);
    }
    HttpResponse resp;
    resp.status = get_status(request);
    std::vector<char> buf(64 * 1024);
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request, buf.data(), (DWORD)buf.size(), &read))
            throw std::runtime_error("connection lost");
        if (read == 0)
            break;
        resp.body.append(buf.data(), read);
    }
    return resp;
}
#else
void http_get(const std::string &, const std::function<void(const char *, size_t, uint64_t)> &, const HttpHeaders &)
{
    throw std::runtime_error("not available on this system yet");
}
HttpResponse http_post(const std::string &, const HttpHeaders &, const std::string &, int)
{
    throw std::runtime_error("not available on this system yet");
}
#endif

} // namespace dune3d

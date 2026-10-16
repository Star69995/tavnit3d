#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace dune3d {

// Minimal HTTPS client (WinHTTP on Windows, libcurl elsewhere; without either these throw).
using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

// GET url, feeding the body to sink; follows redirects. Throws unless the
// answer is 200.
void http_get(const std::string &url, const std::function<void(const char *, size_t, uint64_t total)> &sink,
              const HttpHeaders &headers = {});

struct HttpResponse {
    int status = 0;
    std::string body;
};

// POST body to url; returns whatever the server answered, also errors.
// timeout_s: how long to wait for the answer.
HttpResponse http_post(const std::string &url, const HttpHeaders &headers, const std::string &body,
                       int timeout_s = 60);

} // namespace dune3d

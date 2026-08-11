#pragma once

// Simulator-only override of the crosspoint-simulator shim's SecureHttpClient.
//
// The external shim (crosspoint-reader/crosspoint-simulator) tracks upstream
// CrossPoint and has drifted behind Almanac's freeink-sdk SecureHttpClient:
// TesseraeClient.cpp calls setUserAgent(), responseComplete() and resolveUrl(),
// none of which the shim provides, so [env:simulator] failed to compile. The
// simulator is not built in CI (ci.yml builds default + sticky only), which is
// why the drift went unnoticed.
//
// This is that shim plus those three members. It is installed over the libdep's
// own copy by scripts/patch_simulator_shim.py, a pre: script on [env:simulator].
// An include path cannot do the job: PlatformIO appends build_flags include dirs
// after library ones, so the libdep always wins the lookup for
// <SecureHttpClient.h>. Device builds resolve that name to the real freeink-sdk
// header and never see this file.
// Keep it in sync with the external shim if that gains members we use.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "HTTPClient.h"
#include "NetworkClient.h"
#include "WString.h"

namespace freeink {

class SecureHttpClient {
 public:
  void setCACert(const char*) {}
  void setInsecure() {}

  // Held apart from the per-request headers, like the SDK: the agent is set
  // before begin() and must survive it, so it is applied when the request goes
  // out rather than at the call site.
  void setUserAgent(const std::string& ua) { userAgent_ = ua; }

  bool begin(const String& url) {
    http_.begin(client_, url.c_str());
    return !url.isEmpty();
  }

  bool begin(const std::string& url) { return begin(String(url)); }
  bool begin(const char* url) { return begin(String(url)); }

  void end() { http_.end(); }

  void addHeader(const char* name, const String& value) { http_.addHeader(name, value); }

  void addHeader(const char* name, const std::string& value) { http_.addHeader(name, value.c_str()); }

  void addHeader(const char* name, const char* value) { http_.addHeader(name, value); }

  void setTimeout(uint16_t ms) { http_.setTimeout(ms); }
  void setReuse(bool reuse) { http_.setReuse(reuse); }

  int GET() {
    applyUserAgent();
    return record(http_.GET());
  }

  int POST(const String& payload) {
    applyUserAgent();
    return record(http_.POST(payload.c_str()));
  }

  int sendRequest(const char* method, const String& payload) {
    applyUserAgent();
    if (method && std::string(method) == "PUT") return record(http_.PUT(payload));
    if (method && std::string(method) == "POST") return record(http_.POST(payload.c_str()));
    return record(http_.GET());
  }

  int sendRequest(const char* method, const std::string& payload) { return sendRequest(method, String(payload)); }

  String getString() { return http_.getString(); }
  int getSize() { return http_.getSize(); }

  // The curl-backed shim either delivers the whole body or reports a transport
  // error, so a positive status is exactly "the body arrived complete". The SDK
  // tracks this separately because it streams the body itself.
  bool responseComplete() const { return lastStatus_ > 0; }

  static bool tls13Available() { return true; }

  // Same contract as the SDK's resolveUrl: an absolute location passes through,
  // a root-relative one keeps the base authority, and a relative one resolves
  // against the base path's parent. A port equal to the scheme default is
  // elided, matching the SDK's hostHeaderFor().
  static bool resolveUrl(const std::string& baseUrl, const std::string& location, std::string& resolved) {
    if (location.find("://") != std::string::npos) {
      resolved = location;
      return true;
    }
    std::string scheme;
    std::string host;
    std::string path;
    uint16_t port = 0;
    if (!parseUrl(baseUrl, scheme, host, path, port)) return false;
    const std::string authority = hostHeaderFor(scheme, host, port);
    if (!location.empty() && location[0] == '/') {
      resolved = scheme + "://" + authority + location;
      return true;
    }
    const size_t slash = path.rfind('/');
    const std::string parent = slash == std::string::npos ? "/" : path.substr(0, slash + 1);
    resolved = scheme + "://" + authority + parent + location;
    return true;
  }

 private:
  void applyUserAgent() {
    if (!userAgent_.empty()) http_.addHeader("User-Agent", userAgent_.c_str());
  }

  int record(int status) {
    lastStatus_ = status;
    return status;
  }

  static bool parseUrl(const std::string& url, std::string& scheme, std::string& host, std::string& path,
                       uint16_t& port) {
    const size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;
    // URL schemes are case-insensitive (RFC 3986 3.1).
    scheme = url.substr(0, schemeEnd);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                   [](unsigned char c) { return static_cast<char>(tolower(c)); });
    const size_t hostStart = schemeEnd + 3;
    const size_t pathStart = url.find('/', hostStart);
    const std::string hostPort =
        pathStart == std::string::npos ? url.substr(hostStart) : url.substr(hostStart, pathStart - hostStart);
    path = pathStart == std::string::npos ? "/" : url.substr(pathStart);
    const size_t portSep = hostPort.rfind(':');
    if (portSep != std::string::npos) {
      host = hostPort.substr(0, portSep);
      port = static_cast<uint16_t>(atoi(hostPort.substr(portSep + 1).c_str()));
    } else {
      host = hostPort;
      port = scheme == "https" ? 443 : 80;
    }
    return !host.empty() && (scheme == "http" || scheme == "https");
  }

  static std::string hostHeaderFor(const std::string& scheme, const std::string& host, uint16_t port) {
    const uint16_t defaultPort = scheme == "https" ? 443 : 80;
    if (port == 0 || port == defaultPort) return host;
    return host + ":" + std::to_string(port);
  }

  NetworkClientSecure client_;
  HTTPClient http_;
  std::string userAgent_;
  int lastStatus_ = 0;
};

}  // namespace freeink

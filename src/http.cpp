#include "http.h"
#include <cstdio>
#include "util.h"
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#endif

std::string urlEncodePath(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') o += (char)c;
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

#ifdef _WIN32
static std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, 0);
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

bool httpGet(const std::string& url, std::vector<uint8_t>& out, int* status) {
    out.clear();
    if (status) *status = 0;
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[4096];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 4096;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return false;
    HINTERNET ses = WinHttpOpen(L"UniEmu/1.2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses)  // Windows 7: sem proxy automático
        ses = WinHttpOpen(L"UniEmu/1.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return false;
    WinHttpSetTimeouts(ses, 8000, 8000, 15000, 30000);
    bool ok = false;
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0);
    if (con) {
        DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET req = WinHttpOpenRequest(con, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (req) {
            if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(req, nullptr)) {
                DWORD code = 0, sz = sizeof code;
                WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                    &code, &sz, WINHTTP_NO_HEADER_INDEX);
                if (status) *status = (int)code;
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(req, &avail) && avail) {
                    size_t old = out.size();
                    out.resize(old + avail);
                    DWORD rd = 0;
                    if (!WinHttpReadData(req, out.data() + old, avail, &rd)) break;
                    out.resize(old + rd);
                }
                ok = code == 200;
            }
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(con);
    }
    WinHttpCloseHandle(ses);
    return ok;
}
#else
bool httpGet(const std::string& url, std::vector<uint8_t>& out, int* status) {
    out.clear();
    std::string cmd = "curl -s -L --max-time 30 -w '\\n%{http_code}' '" + url + "'";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return false;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.insert(out.end(), buf, buf + n);
    pclose(p);
    // últimos bytes: "\n<status>"
    size_t nl = std::string(out.begin(), out.end()).rfind('\n');
    int code = 0;
    if (nl != std::string::npos) {
        code = atoi(std::string(out.begin() + nl + 1, out.end()).c_str());
        out.resize(nl);
    }
    if (status) *status = code;
    return code == 200;
}
#endif

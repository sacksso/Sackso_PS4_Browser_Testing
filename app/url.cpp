#include "url.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

bool urlIsHttp(const std::string& u) { return u.compare(0, 7, "http://") == 0 || u.compare(0, 8, "https://") == 0; }

std::string urlStripFragment(const std::string& url) {
    size_t h = url.find('#');
    return h == std::string::npos ? url : url.substr(0, h);
}

// Quita segmentos "." y ".." de una ruta que empieza por '/'.
static std::string normalizePath(const std::string& path) {
    std::string query;
    std::string p = path;
    size_t q = p.find('?');
    if (q != std::string::npos) { query = p.substr(q); p = p.substr(0, q); }
    size_t i = 0;
    std::string result;
    std::string cur;
    std::string stack[256];  // segmentos de la ruta
    int n = 0;
    bool trailingSlash = !p.empty() && p.back() == '/';
    for (i = 0; i <= p.size(); i++) {
        if (i == p.size() || p[i] == '/') {
            if (cur == "..") { if (n > 0) n--; }
            else if (!cur.empty() && cur != ".") { if (n < 256) stack[n++] = cur; }
            cur.clear();
        } else {
            cur += p[i];
        }
    }
    for (int k = 0; k < n; k++) { result += "/"; result += stack[k]; }
    if (result.empty() || (trailingSlash && n > 0)) result += "/";
    return result + query;
}

std::string urlResolve(const std::string& base, const std::string& ref) {
    if (ref.empty()) return base;
    if (urlIsHttp(ref)) return ref;
    size_t schemeEnd = base.find("://");
    if (schemeEnd == std::string::npos) return ref;
    std::string scheme = base.substr(0, schemeEnd + 1);  // "https:"
    size_t hostEnd = base.find('/', schemeEnd + 3);
    std::string origin = (hostEnd == std::string::npos) ? base : base.substr(0, hostEnd);
    std::string basePath = (hostEnd == std::string::npos) ? "/" : base.substr(hostEnd);
    size_t frag = basePath.find('#');
    if (frag != std::string::npos) basePath = basePath.substr(0, frag);
    if (ref.compare(0, 2, "//") == 0) return scheme + ref;
    if (ref[0] == '/') return origin + normalizePath(ref);
    if (ref[0] == '#') return urlStripFragment(base) + ref;
    if (ref[0] == '?') {
        size_t q = basePath.find('?');
        return origin + (q == std::string::npos ? basePath : basePath.substr(0, q)) + ref;
    }
    size_t q = basePath.find('?');
    if (q != std::string::npos) basePath = basePath.substr(0, q);
    size_t slash = basePath.rfind('/');
    std::string dir = (slash == std::string::npos) ? "/" : basePath.substr(0, slash + 1);
    return origin + normalizePath(dir + ref);
}

std::string urlPercentEncode(const std::string& s) {
    std::string o;
    char buf[4];
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c);
        else if (c == ' ') o += '+';
        else { std::snprintf(buf, sizeof(buf), "%%%02X", c); o += buf; }
    }
    return o;
}

std::string urlFromUserInput(const std::string& in) {
    size_t a = in.find_first_not_of(' ');
    size_t b = in.find_last_not_of(' ');
    if (a == std::string::npos) return std::string();
    std::string s = in.substr(a, b - a + 1);
    if (urlIsHttp(s) || s.compare(0, 6, "about:") == 0) return s;
    bool hasSpace = s.find(' ') != std::string::npos;
    bool hasDot = s.find('.') != std::string::npos;
    if (!hasSpace && (hasDot || s.compare(0, 9, "localhost") == 0)) return "https://" + s;
    return "https://html.duckduckgo.com/html/?q=" + urlPercentEncode(s);
}

std::string urlPercentDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            char hex[3] = {s[i + 1], s[i + 2], 0};
            o += static_cast<char>(std::strtol(hex, nullptr, 16));
            i += 2;
        } else if (s[i] == '+') {
            o += ' ';
        } else {
            o += s[i];
        }
    }
    return o;
}
std::string urlUnwrapSearchRedirect(const std::string& url) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return std::string();
    size_t hostEnd = url.find('/', schemeEnd + 3);
    if (hostEnd == std::string::npos) return std::string();
    std::string host = url.substr(schemeEnd + 3, hostEnd - schemeEnd - 3);
    bool ddg = host == "duckduckgo.com" || host == "www.duckduckgo.com" || host == "html.duckduckgo.com" || host == "lite.duckduckgo.com";
    if (!ddg || url.compare(hostEnd, 3, "/l/") != 0) return std::string();
    size_t u = url.find("uddg=", hostEnd);
    if (u == std::string::npos) return std::string();
    size_t e = url.find('&', u);
    std::string dec = urlPercentDecode(url.substr(u + 5, e == std::string::npos ? std::string::npos : e - u - 5));
    return urlIsHttp(dec) ? dec : std::string();
}

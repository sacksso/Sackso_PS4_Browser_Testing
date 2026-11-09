// Almacen de cookies (ver cookies.h).
#include "cookies.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

bool g_cookiesEnabled = true;

namespace {
struct Cookie {
    std::string name, value, domain, path;
    bool hostOnly = true, secure = false, httpOnly = false;
    long long expires = 0;   // 0 = de sesion
    unsigned long long seq = 0;
};
std::mutex g_mu;
std::vector<Cookie> g_jar;
std::vector<std::pair<std::string, std::string>> g_ua;
std::string g_cookiePath = "/data/psbrowser/cookies.txt", g_uaPath = "/data/psbrowser/ua_overrides.txt";
std::function<long long()> g_clock;
bool g_dirty = false;
unsigned long long g_seq = 0;
const size_t kMaxCookies = 3000;
const size_t kMaxCookieBytes = 4096;

long long nowSec() { return g_clock ? g_clock() : static_cast<long long>(time(nullptr)); }
std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}
bool endsWith(const std::string& s, const std::string& suf) { return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0; }
bool domainMatch(const std::string& host, const std::string& dom, bool hostOnly) {
    if (host == dom) return true;
    if (hostOnly) return false;
    return host.size() > dom.size() && endsWith(host, dom) && host[host.size() - dom.size() - 1] == '.';
}
bool pathMatch(const std::string& reqPath, const std::string& cp) {
    if (reqPath == cp) return true;
    if (reqPath.compare(0, cp.size(), cp) != 0) return false;
    return cp.back() == '/' || reqPath[cp.size()] == '/';
}
std::string defaultPath(const std::string& p) {
    if (p.empty() || p[0] != '/') return "/";
    const size_t i = p.rfind('/');
    return i == 0 ? "/" : p.substr(0, i);
}
bool isIp(const std::string& h) {
    if (h.empty()) return false;
    if (h.find(':') != std::string::npos) return true;
    for (char c : h) if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.')) return false;
    return true;
}
}  // namespace

bool url_split(const std::string& url, std::string& scheme, std::string& host, std::string& path) {
    const size_t se = url.find("://");
    if (se == std::string::npos) return false;
    scheme = lower(url.substr(0, se));
    size_t hs = se + 3, he = url.find_first_of("/?#", hs);
    std::string auth = url.substr(hs, he == std::string::npos ? std::string::npos : he - hs);
    const size_t at = auth.rfind('@');
    if (at != std::string::npos) auth = auth.substr(at + 1);
    if (!auth.empty() && auth[0] == '[') { const size_t rb = auth.find(']'); host = lower(auth.substr(0, rb == std::string::npos ? auth.size() : rb + 1)); }
    else { const size_t colon = auth.find(':'); host = lower(colon == std::string::npos ? auth : auth.substr(0, colon)); }
    path = "/";
    if (he != std::string::npos && url[he] == '/') {
        const size_t pe = url.find_first_of("?#", he);
        path = url.substr(he, pe == std::string::npos ? std::string::npos : pe - he);
    }
    return !host.empty();
}

long long http_date_to_epoch(const std::string& s0) {
    // "Wed, 21 Oct 2026 07:28:00 GMT", "Wed, 21-Oct-26 07:28:00 GMT", "Wed Oct 21 07:28:00 2026"
    std::string s = s0;
    for (auto& c : s) if (c == '-' || c == ',') c = ' ';
    static const char* mons[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
    std::vector<std::string> t;
    { std::string cur; for (char c : s) { if (c == ' ') { if (!cur.empty()) t.push_back(cur); cur.clear(); } else cur += c; } if (!cur.empty()) t.push_back(cur); }
    int day = -1, mon = -1, year = -1, hh = 0, mm = 0, ss = 0;
    for (const std::string& w : t) {
        if (w.find(':') != std::string::npos) { if (sscanf(w.c_str(), "%d:%d:%d", &hh, &mm, &ss) < 2) return -1; continue; }
        if (std::isdigit(static_cast<unsigned char>(w[0]))) {
            const int v = atoi(w.c_str());
            if (w.size() >= 3 || v > 31) year = v; else if (day < 0) day = v; else year = v < 70 ? 2000 + v : 1900 + v;
            continue;
        }
        const std::string lw = lower(w.substr(0, 3));
        for (int i = 0; i < 12; i++) if (lw == mons[i]) mon = i;
    }
    if (day < 1 || day > 31 || mon < 0 || year < 1970) return -1;
    // dias desde 1970-01-01 (algoritmo civil)
    long long y = year, m = mon + 1;
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days = era * 146097 + doe - 719468;
    return days * 86400 + hh * 3600 + mm * 60 + ss;
}

void cookies_set_clock(std::function<long long()> f) { g_clock = f; }
void cookies_set_path(const std::string& c, const std::string& u) { g_cookiePath = c; g_uaPath = u; }

static void purgeExpiredLocked() {
    const long long n = nowSec();
    g_jar.erase(std::remove_if(g_jar.begin(), g_jar.end(), [n](const Cookie& c) { return c.expires > 0 && c.expires <= n; }), g_jar.end());
}
static void putLocked(const Cookie& c) {
    for (Cookie& x : g_jar) if (x.name == c.name && x.domain == c.domain && x.path == c.path && x.hostOnly == c.hostOnly) { Cookie n = c; n.seq = x.seq; x = n; g_dirty = true; return; }
    Cookie n = c; n.seq = ++g_seq;
    g_jar.push_back(n);
    if (g_jar.size() > kMaxCookies) {  // se descartan las mas antiguas
        auto it = std::min_element(g_jar.begin(), g_jar.end(), [](const Cookie& a, const Cookie& b) { return a.seq < b.seq; });
        g_jar.erase(it);
    }
    g_dirty = true;
}
static void delLocked(const Cookie& c) {
    const size_t before = g_jar.size();
    g_jar.erase(std::remove_if(g_jar.begin(), g_jar.end(), [&](const Cookie& x) { return x.name == c.name && x.domain == c.domain && x.path == c.path; }), g_jar.end());
    if (g_jar.size() != before) g_dirty = true;
}

bool cookies_store_one(const std::string& url, const std::string& sc, bool fromScript) {
    if (!g_cookiesEnabled || sc.empty() || sc.size() > kMaxCookieBytes) return false;
    std::string scheme, host, path;
    if (!url_split(url, scheme, host, path)) return false;
    Cookie c;
    c.domain = host; c.hostOnly = true; c.path = defaultPath(path);
    bool haveMaxAge = false;
    long long maxAge = 0, expires = 0;
    size_t pos = 0;
    bool first = true;
    while (pos <= sc.size()) {
        size_t semi = sc.find(';', pos);
        if (semi == std::string::npos) semi = sc.size();
        const std::string part = trim(sc.substr(pos, semi - pos));
        pos = semi + 1;
        if (first) {
            first = false;
            const size_t eq = part.find('=');
            if (eq == std::string::npos) return false;
            c.name = trim(part.substr(0, eq)); c.value = trim(part.substr(eq + 1));
            if (c.name.empty()) return false;
            continue;
        }
        const size_t eq = part.find('=');
        const std::string k = lower(trim(eq == std::string::npos ? part : part.substr(0, eq)));
        const std::string v = eq == std::string::npos ? "" : trim(part.substr(eq + 1));
        if (k == "domain") {
            std::string d = lower(v);
            if (!d.empty() && d[0] == '.') d = d.substr(1);
            if (d.empty() || isIp(d) || d.find('.') == std::string::npos) return false;   // sin sufijos publicos tipo "com"
            if (!domainMatch(host, d, false)) return false;                            // un sitio no puede poner cookies de otro dominio
            c.domain = d; c.hostOnly = false;
        } else if (k == "path") { if (!v.empty() && v[0] == '/') c.path = v; }
        else if (k == "expires") expires = http_date_to_epoch(v);
        else if (k == "max-age") { haveMaxAge = true; maxAge = atoll(v.c_str()); }
        else if (k == "secure") c.secure = true;
        else if (k == "httponly") c.httpOnly = true;
        if (pos > sc.size()) break;
    }
    if (c.secure && scheme != "https") return false;
    if (fromScript && c.httpOnly) return false;
    const long long now = nowSec();
    bool expired = false;
    if (haveMaxAge) { if (maxAge <= 0) expired = true; else c.expires = now + maxAge; }
    else if (expires > 0) { if (expires <= now) expired = true; else c.expires = expires; }
    std::lock_guard<std::mutex> lk(g_mu);
    if (expired) { delLocked(c); return true; }
    putLocked(c);
    return true;
}

int cookies_store_from_headers(const std::string& url, const char* hdrs, size_t len) {
    if (!g_cookiesEnabled || !hdrs) return 0;
    int n = 0;
    const char* p = hdrs; const char* end = hdrs + len;
    while (p < end) {
        const char* eol = static_cast<const char*>(memchr(p, '\n', static_cast<size_t>(end - p)));
        const char* le = eol ? eol : end;
        if (static_cast<size_t>(le - p) > 11 && lower(std::string(p, 11)) == "set-cookie:") {
            std::string v(p + 11, le);
            if (cookies_store_one(url, trim(v))) n++;
        }
        if (!eol) break;
        p = eol + 1;
    }
    return n;
}

std::string cookies_header(const std::string& url) {
    if (!g_cookiesEnabled) return std::string();
    std::string scheme, host, path;
    if (!url_split(url, scheme, host, path)) return std::string();
    std::vector<Cookie> m;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        purgeExpiredLocked();
        for (const Cookie& c : g_jar) {
            if (!domainMatch(host, c.domain, c.hostOnly) || !pathMatch(path, c.path)) continue;
            if (c.secure && scheme != "https") continue;
            m.push_back(c);
        }
    }
    std::sort(m.begin(), m.end(), [](const Cookie& a, const Cookie& b) { return a.path.size() != b.path.size() ? a.path.size() > b.path.size() : a.seq < b.seq; });
    std::string h;
    for (const Cookie& c : m) { if (!h.empty()) h += "; "; h += c.name + "=" + c.value; }
    return h;
}

std::string cookies_for_script(const std::string& url) {
    if (!g_cookiesEnabled) return std::string();
    std::string scheme, host, path;
    if (!url_split(url, scheme, host, path)) return std::string();
    std::vector<Cookie> m;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        purgeExpiredLocked();
        for (const Cookie& c : g_jar) {
            if (c.httpOnly || !domainMatch(host, c.domain, c.hostOnly) || !pathMatch(path, c.path)) continue;
            if (c.secure && scheme != "https") continue;
            m.push_back(c);
        }
    }
    std::sort(m.begin(), m.end(), [](const Cookie& a, const Cookie& b) { return a.path.size() != b.path.size() ? a.path.size() > b.path.size() : a.seq < b.seq; });
    std::string h;
    for (const Cookie& c : m) { if (!h.empty()) h += "; "; h += c.name + "=" + c.value; }
    return h;
}
void cookies_set_from_script(const std::string& url, const std::string& cookie) { cookies_store_one(url, cookie, true); }

size_t cookies_count() { std::lock_guard<std::mutex> lk(g_mu); return g_jar.size(); }

std::string cookies_export_netscape() {
    std::lock_guard<std::mutex> lk(g_mu);
    purgeExpiredLocked();
    std::string o = "# Netscape HTTP Cookie File\n# psbrowser\n";
    for (const Cookie& c : g_jar) {
        if (c.httpOnly) o += "#HttpOnly_";
        o += (c.hostOnly ? "" : ".") + c.domain + "\t" + (c.hostOnly ? "FALSE" : "TRUE") + "\t" + c.path + "\t" + (c.secure ? "TRUE" : "FALSE") + "\t" + std::to_string(c.expires) + "\t" + c.name + "\t" + c.value + "\n";
    }
    return o;
}

int cookies_import_netscape(const std::string& text) {
    int n = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        bool httpOnly = false;
        if (line.compare(0, 10, "#HttpOnly_") == 0) { httpOnly = true; line = line.substr(10); }
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        { size_t a = 0; for (;;) { const size_t t = line.find('\t', a); if (t == std::string::npos) { f.push_back(line.substr(a)); break; } f.push_back(line.substr(a, t - a)); a = t + 1; } }
        if (f.size() < 7) continue;
        Cookie c;
        c.domain = lower(f[0]);
        c.hostOnly = true;
        if (!c.domain.empty() && c.domain[0] == '.') { c.domain = c.domain.substr(1); c.hostOnly = false; }
        if (lower(f[1]) == "true") c.hostOnly = false;
        c.path = f[2].empty() ? "/" : f[2];
        c.secure = lower(f[3]) == "true";
        c.expires = atoll(f[4].c_str());
        c.name = f[5];
        for (size_t i = 7; i < f.size(); i++) f[6] += "\t" + f[i];
        c.value = f[6];
        c.httpOnly = httpOnly;
        if (c.domain.empty() || c.name.empty() || c.value.size() > kMaxCookieBytes) continue;
        if (c.expires > 0 && c.expires <= nowSec()) continue;
        std::lock_guard<std::mutex> lk(g_mu);
        putLocked(c);
        n++;
    }
    return n;
}

int cookies_import_header(const std::string& domain, const std::string& header) {
    std::string h = header;
    if (lower(h.substr(0, 7)) == "cookie:") h = h.substr(7);
    std::string d = lower(trim(domain));
    if (!d.empty() && d[0] == '.') d = d.substr(1);
    if (d.empty()) return 0;
    int n = 0;
    size_t pos = 0;
    while (pos < h.size()) {
        size_t semi = h.find(';', pos);
        if (semi == std::string::npos) semi = h.size();
        const std::string part = trim(h.substr(pos, semi - pos));
        pos = semi + 1;
        const size_t eq = part.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        Cookie c;
        c.name = trim(part.substr(0, eq)); c.value = trim(part.substr(eq + 1));
        c.domain = d; c.hostOnly = false; c.path = "/";
        std::lock_guard<std::mutex> lk(g_mu);
        putLocked(c);
        n++;
    }
    return n;
}

void cookies_clear(const std::string& domain) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (domain.empty()) { if (!g_jar.empty()) g_dirty = true; g_jar.clear(); return; }
    std::string d = lower(domain);
    if (!d.empty() && d[0] == '.') d = d.substr(1);
    const size_t before = g_jar.size();
    g_jar.erase(std::remove_if(g_jar.begin(), g_jar.end(), [&](const Cookie& c) { return c.domain == d || endsWith(c.domain, "." + d) || endsWith(d, "." + c.domain); }), g_jar.end());
    if (g_jar.size() != before) g_dirty = true;
}

std::vector<CookieDomainInfo> cookies_domains() {
    std::vector<CookieDomainInfo> v;
    std::lock_guard<std::mutex> lk(g_mu);
    purgeExpiredLocked();
    for (const Cookie& c : g_jar) {
        bool found = false;
        for (CookieDomainInfo& i : v) if (i.domain == c.domain) { i.count++; found = true; break; }
        if (!found) { CookieDomainInfo i; i.domain = c.domain; i.count = 1; v.push_back(i); }
    }
    std::sort(v.begin(), v.end(), [](const CookieDomainInfo& a, const CookieDomainInfo& b) { return a.domain < b.domain; });
    return v;
}

static void uaSaveLocked() {
    FILE* f = fopen(g_uaPath.c_str(), "w");
    if (!f) return;
    for (auto& p : g_ua) fprintf(f, "%s\t%s\n", p.first.c_str(), p.second.c_str());
    fclose(f);
}
std::string ua_override_for(const std::string& host) {
    std::lock_guard<std::mutex> lk(g_mu);
    const std::string h = lower(host);
    size_t bestLen = 0; std::string best;
    for (auto& p : g_ua) if ((h == p.first || endsWith(h, "." + p.first)) && p.first.size() >= bestLen) { bestLen = p.first.size(); best = p.second; }
    return best;
}
void ua_override_set(const std::string& domain, const std::string& ua) {
    std::string d = lower(trim(domain));
    if (!d.empty() && d[0] == '.') d = d.substr(1);
    if (d.empty()) return;
    std::lock_guard<std::mutex> lk(g_mu);
    g_ua.erase(std::remove_if(g_ua.begin(), g_ua.end(), [&](const std::pair<std::string, std::string>& p) { return p.first == d; }), g_ua.end());
    if (!ua.empty()) g_ua.push_back({d, ua.substr(0, 400)});
    uaSaveLocked();
}
std::vector<std::pair<std::string, std::string>> ua_overrides() { std::lock_guard<std::mutex> lk(g_mu); return g_ua; }

void cookies_load() {
    FILE* f = fopen(g_cookiePath.c_str(), "rb");
    if (f) {
        std::string t; char buf[4096]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) t.append(buf, n);
        fclose(f);
        const int k = cookies_import_netscape(t);
        std::lock_guard<std::mutex> lk(g_mu);
        g_dirty = false;
        (void)k;
    }
    f = fopen(g_uaPath.c_str(), "r");
    if (f) {
        char line[900];
        std::lock_guard<std::mutex> lk(g_mu);
        g_ua.clear();
        while (fgets(line, sizeof(line), f)) {
            std::string l = line;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            const size_t t = l.find('\t');
            if (t != std::string::npos && t > 0) g_ua.push_back({lower(l.substr(0, t)), l.substr(t + 1)});
        }
        fclose(f);
    }
}
void cookies_save() {
    std::string text;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (!g_dirty) return;
    }
    text = cookies_export_netscape();
    FILE* f = fopen(g_cookiePath.c_str(), "wb");
    if (!f) return;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    std::lock_guard<std::mutex> lk(g_mu);
    g_dirty = false;
}

#include "download.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// %XX -> byte ('+' se deja tal cual: en una ruta es un '+' de verdad).
std::string pctDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() + 0 && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) {
            o += static_cast<char>(hexv(s[i + 1]) * 16 + hexv(s[i + 2]));
            i += 2;
        } else {
            o += s[i];
        }
    }
    return o;
}
// Valor del parametro 'name' en "attachment; filename="a b.zip"; filename*=UTF-8''a%20b.zip". Quita comillas.
std::string cdParam(const std::string& cd, const char* name) {
    const std::string low = lower(cd);
    const std::string key = std::string(name) + "=";
    size_t pos = 0;
    while ((pos = low.find(key, pos)) != std::string::npos) {
        // debe ir tras ';' o espacio (o ser el inicio), para no confundir "filename" con "xfilename"
        const bool okStart = pos == 0 || low[pos - 1] == ';' || low[pos - 1] == ' ' || low[pos - 1] == '\t';
        // y "filename=" no debe ser el final de "filename*="; como buscamos "filename=" literal, "filename*=" no coincide.
        if (okStart) {
            size_t v = pos + key.size();
            std::string val;
            if (v < cd.size() && cd[v] == '"') {
                v++;
                while (v < cd.size() && cd[v] != '"') {
                    if (cd[v] == '\\' && v + 1 < cd.size()) v++;
                    val += cd[v++];
                }
            } else {
                while (v < cd.size() && cd[v] != ';') val += cd[v++];
                while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r')) val.pop_back();
            }
            return val;
        }
        pos += key.size();
    }
    return std::string();
}

const char* extForType(const std::string& ctLow) {
    struct M { const char* t; const char* e; };
    static const M m[] = {
        {"image/png", ".png"}, {"image/jpeg", ".jpg"}, {"image/jpg", ".jpg"}, {"image/gif", ".gif"}, {"image/webp", ".webp"},
        {"image/svg+xml", ".svg"}, {"image/bmp", ".bmp"}, {"image/x-icon", ".ico"}, {"image/vnd.microsoft.icon", ".ico"},
        {"image/avif", ".avif"}, {"video/mp4", ".mp4"}, {"video/webm", ".webm"}, {"video/quicktime", ".mov"},
        {"video/x-matroska", ".mkv"}, {"audio/mpeg", ".mp3"}, {"audio/mp4", ".m4a"}, {"audio/ogg", ".ogg"}, {"audio/wav", ".wav"},
        {"application/pdf", ".pdf"}, {"application/zip", ".zip"}, {"application/x-zip-compressed", ".zip"},
        {"application/x-7z-compressed", ".7z"}, {"application/vnd.rar", ".rar"}, {"application/x-rar-compressed", ".rar"},
        {"application/gzip", ".gz"}, {"application/x-tar", ".tar"}, {"application/json", ".json"},
        {"application/vnd.android.package-archive", ".apk"}, {"text/plain", ".txt"}, {"text/csv", ".csv"},
    };
    for (const M& x : m)
        if (ctLow.compare(0, std::strlen(x.t), x.t) == 0) return x.e;
    return "";
}

bool hasExt(const std::string& n) {
    const size_t d = n.rfind('.');
    return d != std::string::npos && d > 0 && n.size() - d <= 8 && n.size() - d >= 2;
}

}  // namespace

bool dl_is_page_type(const std::string& contentType) {
    if (contentType.empty()) return true;
    const std::string c = lower(contentType);
    return has(c, "html") || c.compare(0, 5, "text/") == 0 || has(c, "json") || has(c, "xml") || has(c, "javascript");
}

bool dl_is_previewable_image(const std::string& contentType) {
    const std::string c = lower(contentType);
    return c.compare(0, 9, "image/png") == 0 || c.compare(0, 10, "image/jpeg") == 0 || c.compare(0, 9, "image/jpg") == 0 ||
           c.compare(0, 9, "image/bmp") == 0;
}

bool dl_is_streaming(const std::string& contentType, const std::string& url) {
    const std::string c = lower(contentType);
    if (has(c, "mpegurl") || has(c, "dash+xml")) return true;
    std::string p = lower(url.substr(0, url.find_first_of("?#")));
    auto ends = [&](const char* e) { const size_t n = std::strlen(e); return p.size() >= n && p.compare(p.size() - n, n, e) == 0; };
    return ends(".m3u8") || ends(".mpd");
}

bool dl_is_attachment(const std::string& contentDisposition) {
    return has(lower(contentDisposition), "attachment");
}

std::string dl_sanitize_name(const std::string& raw) {
    std::string o;
    for (unsigned char c : raw) {
        if (c < 32 || c == 127) continue;
        if (std::isalnum(c) || c >= 0x80 || c == '.' || c == '-' || c == '_' || c == '(' || c == ')' || c == '[' || c == ']' ||
            c == '+' || c == ' ' || c == ',' || c == '\'')
            o += static_cast<char>(c);
        else
            o += '_';  // '/', '\\', ':', '*', '?', '"', '<', '>', '|', ';', '&', '=', '%', '#'...
    }
    // sin puntos/espacios al principio ni al final (evita ".", ".." y archivos ocultos)
    size_t a = 0;
    while (a < o.size() && (o[a] == '.' || o[a] == ' ')) a++;
    size_t b = o.size();
    while (b > a && (o[b - 1] == '.' || o[b - 1] == ' ')) b--;
    o = o.substr(a, b - a);
    // maximo 120 bytes sin cortar un caracter UTF-8 por la mitad; conserva la extension
    if (o.size() > 120) {
        std::string ext;
        const size_t d = o.rfind('.');
        if (d != std::string::npos && o.size() - d <= 10) ext = o.substr(d);
        size_t keep = 120 - ext.size();
        std::string stem = o.substr(0, d != std::string::npos && !ext.empty() ? d : o.size());
        if (stem.size() > keep) {
            stem.resize(keep);
            while (!stem.empty() && (static_cast<unsigned char>(stem.back()) & 0xC0) == 0x80) stem.pop_back();  // continuacion UTF-8
            if (!stem.empty() && static_cast<unsigned char>(stem.back()) >= 0xC0) stem.pop_back();             // cabecera suelta
        }
        o = stem + ext;
    }
    return o;
}

std::string dl_filename(const std::string& url, const std::string& cd, const std::string& ct) {
    std::string name;
    // 1) Content-Disposition: filename*=UTF-8''... (RFC 5987) y si no filename="..."
    std::string v = cdParam(cd, "filename*");
    if (!v.empty()) {
        const size_t q = v.find("''");
        name = pctDecode(q == std::string::npos ? v : v.substr(q + 2));
    }
    if (dl_sanitize_name(name).empty()) {
        name = cdParam(cd, "filename");
        if (name.find('%') != std::string::npos) name = pctDecode(name);
    }
    // 2) ultimo segmento de la ruta de la URL
    if (dl_sanitize_name(name).empty()) {
        std::string p = url.substr(0, url.find_first_of("?#"));
        const size_t s = p.find("://");
        const size_t pathStart = p.find('/', s == std::string::npos ? 0 : s + 3);
        p = pathStart == std::string::npos ? std::string() : p.substr(pathStart);
        const size_t sl = p.rfind('/');
        name = pctDecode(sl == std::string::npos ? p : p.substr(sl + 1));
    }
    name = dl_sanitize_name(name);
    const std::string ctLow = lower(ct);
    const std::string ext = extForType(ctLow);
    // scripts de servidor (download.php?id=3): el nombre real no es ".php"
    {
        const size_t d = name.rfind('.');
        if (d != std::string::npos && !ext.empty()) {
            const std::string e = lower(name.substr(d));
            if (e == ".php" || e == ".asp" || e == ".aspx" || e == ".jsp" || e == ".cgi" || e == ".do" || e == ".html" || e == ".htm")
                name = name.substr(0, d);
        }
    }
    if (name.empty()) name = "descarga";
    if (!hasExt(name) && !ext.empty()) name += ext;
    return name;
}

std::string dl_unique_path(const std::string& dir, const std::string& name) {
    auto exists = [](const std::string& p) {
        FILE* f = std::fopen(p.c_str(), "rb");
        if (f) { std::fclose(f); return true; }
        return false;
    };
    std::string path = dir + "/" + name;
    if (!exists(path)) return path;
    const size_t d = name.rfind('.');
    const bool ext = d != std::string::npos && d > 0 && name.size() - d <= 10;
    const std::string stem = ext ? name.substr(0, d) : name, tail = ext ? name.substr(d) : std::string();
    for (int i = 1; i < 1000; i++) {
        path = dir + "/" + stem + " (" + std::to_string(i) + ")" + tail;
        if (!exists(path)) return path;
    }
    return dir + "/" + stem + " (x)" + tail;
}

std::string dl_format_size(long long b) {
    char t[48];
    if (b < 0) return "?";
    if (b < 1024) std::snprintf(t, sizeof(t), "%lld B", b);
    else if (b < 1024LL * 1024) std::snprintf(t, sizeof(t), "%.1f KB", b / 1024.0);
    else if (b < 1024LL * 1024 * 1024) std::snprintf(t, sizeof(t), "%.1f MB", b / (1024.0 * 1024));
    else std::snprintf(t, sizeof(t), "%.2f GB", b / (1024.0 * 1024 * 1024));
    return t;
}

std::string dl_type_label(const std::string& contentType) {
    const std::string c = lower(contentType);
    if (c.empty()) return "Archivo";
    if (c.compare(0, 6, "image/") == 0) return "Imagen (" + contentType.substr(6, contentType.find(';') == std::string::npos ? std::string::npos : contentType.find(';') - 6) + ")";
    if (c.compare(0, 6, "video/") == 0) return "Video (" + contentType.substr(6, contentType.find(';') == std::string::npos ? std::string::npos : contentType.find(';') - 6) + ")";
    if (c.compare(0, 6, "audio/") == 0) return "Audio (" + contentType.substr(6, contentType.find(';') == std::string::npos ? std::string::npos : contentType.find(';') - 6) + ")";
    if (c.compare(0, 15, "application/pdf") == 0) return "Documento PDF";
    if (has(c, "zip") || has(c, "rar") || has(c, "7z") || has(c, "tar") || has(c, "gzip")) return "Archivo comprimido";
    return contentType.substr(0, contentType.find(';'));
}

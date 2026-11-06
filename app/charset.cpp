#include "charset.h"

#include <cctype>
#include <cstdint>

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// charset=xxx dentro de 'h' (Content-Type o un <meta>), o "".
std::string findCharset(const std::string& h) {
    const std::string l = lower(h);
    const size_t p = l.find("charset");
    if (p == std::string::npos) return std::string();
    size_t i = p + 7;
    while (i < l.size() && (l[i] == ' ' || l[i] == '\t')) i++;
    if (i >= l.size() || l[i] != '=') return std::string();
    i++;
    while (i < l.size() && (l[i] == ' ' || l[i] == '"' || l[i] == '\'')) i++;
    size_t e = i;
    while (e < l.size() && (std::isalnum(static_cast<unsigned char>(l[e])) || l[e] == '-' || l[e] == '_' || l[e] == ':')) e++;
    return l.substr(i, e - i);
}
// Windows-1252 0x80..0x9F -> codepoint Unicode (0 = sin asignar: se usa el mismo valor que Latin-1).
const uint16_t kCp1252[32] = {0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
                              0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
void put(std::string& o, uint32_t cp) {
    if (cp < 0x80) o += static_cast<char>(cp);
    else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
}
}  // namespace

bool is_valid_utf8(const std::string& s) {
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int extra;
        if (c < 0x80) { i++; continue; }
        else if (c >= 0xC2 && c <= 0xDF) extra = 1;
        else if (c >= 0xE0 && c <= 0xEF) extra = 2;
        else if (c >= 0xF0 && c <= 0xF4) extra = 3;
        else return false;
        if (i + static_cast<size_t>(extra) >= n) return false;
        for (int k = 1; k <= extra; k++)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += extra + 1;
    }
    return true;
}

std::string charset_detect(const std::string& contentType, const std::string& head) {
    std::string cs = findCharset(contentType);
    if (!cs.empty()) return cs;
    return findCharset(head.substr(0, 2048));
}

bool charset_to_utf8(std::string& body, const std::string& detected) {
    const std::string d = lower(detected);
    const bool latin = d == "iso-8859-1" || d == "latin1" || d == "iso8859-1" || d == "l1" || d == "windows-1252" || d == "cp1252" ||
                       d == "iso-8859-15" || d == "us-ascii" || d == "ascii" || d == "x-user-defined";
    const bool utf8 = d.empty() || d == "utf-8" || d == "utf8";
    if (utf8) {
        if (is_valid_utf8(body)) return false;
        // Sin charset declarado (o declarado mal) y con bytes invalidos: casi seguro es Windows-1252.
    } else if (!latin) {
        return false;  // otra codificacion (UTF-16, CJK...): sin soporte, se deja tal cual
    }
    std::string out;
    out.reserve(body.size() + body.size() / 8);
    for (unsigned char c : body) {
        if (c < 0x80) out += static_cast<char>(c);
        else if (c >= 0x80 && c < 0xA0) { const uint16_t cp = kCp1252[c - 0x80]; put(out, cp ? cp : c); }
        else if (d == "iso-8859-15") {
            uint32_t cp = c;
            switch (c) { case 0xA4: cp = 0x20AC; break; case 0xA6: cp = 0x160; break; case 0xA8: cp = 0x161; break; case 0xB4: cp = 0x17D; break;
                         case 0xB8: cp = 0x17E; break; case 0xBC: cp = 0x152; break; case 0xBD: cp = 0x153; break; case 0xBE: cp = 0x178; break; default: break; }
            put(out, cp);
        } else put(out, c);
    }
    body.swap(out);
    return true;
}

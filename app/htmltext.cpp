#include "htmltext.h"

#include <cctype>
#include <cstring>

static std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static bool isBlockTag(const std::string& name) {
    static const char* blocks[] = {"p", "div", "br", "li", "ul", "ol", "tr", "table", "h1", "h2", "h3", "h4", "h5", "h6",
                                   "section", "article", "header", "footer", "nav", "pre", "blockquote", "hr", "title"};
    for (const char* b : blocks)
        if (name == b) return true;
    return false;
}

std::vector<std::string> htmlToLines(const std::string& html, size_t cols) {
    std::string out;
    out.reserve(html.size() / 2);
    size_t i = 0;
    const size_t n = html.size();
    bool lastSpace = true;
    while (i < n) {
        char c = html[i];
        if (c == '<') {
            size_t e = html.find('>', i);
            if (e == std::string::npos) break;
            std::string tag = html.substr(i + 1, e - i - 1);
            size_t k = 0;
            bool closing = false;
            if (!tag.empty() && tag[0] == '/') { closing = true; k = 1; }
            size_t s = k;
            while (k < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[k])) || tag[k] == '!')) k++;
            std::string name = lower(tag.substr(s, k - s));
            i = e + 1;
            if (!closing && (name == "script" || name == "style")) {
                std::string endTag = "</" + name;
                size_t z = lower(html.substr(i)).find(endTag);
                if (z == std::string::npos) break;
                size_t z2 = html.find('>', i + z);
                i = (z2 == std::string::npos) ? n : z2 + 1;
                continue;
            }
            if (name.compare(0, 3, "!--") == 0) {  // comentario
                size_t z = html.find("-->", i - 1);
                i = (z == std::string::npos) ? n : z + 3;
                continue;
            }
            if (isBlockTag(name)) { out += '\n'; lastSpace = true; }
            continue;
        }
        if (c == '&') {
            static const struct { const char* ent; const char* rep; } ents[] = {
                {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&apos;", "'"}, {"&nbsp;", " "}};
            bool done = false;
            for (const auto& e : ents) {
                size_t L = std::strlen(e.ent);
                if (html.compare(i, L, e.ent) == 0) { out += e.rep; i += L; done = true; lastSpace = false; break; }
            }
            if (done) continue;
        }
        if (c == '\r') { i++; continue; }
        if (c == '\n' || c == '\t' || c == ' ') {
            if (!lastSpace) { out += ' '; lastSpace = true; }
            i++;
            continue;
        }
        // Solo ASCII imprimible: la fuente 8x8 no tiene mas; el resto se sustituye por '?'.
        out += (static_cast<unsigned char>(c) < 128) ? c : '?';
        lastSpace = false;
        i++;
    }

    // Partir en lineas y ajustar a 'cols' columnas por palabras.
    std::vector<std::string> lines;
    size_t start = 0;
    bool prevBlank = true;
    while (start <= out.size()) {
        size_t nl = out.find('\n', start);
        std::string para = out.substr(start, (nl == std::string::npos ? out.size() : nl) - start);
        size_t a = para.find_first_not_of(' ');
        size_t b = para.find_last_not_of(' ');
        para = (a == std::string::npos) ? std::string() : para.substr(a, b - a + 1);
        if (para.empty()) {
            if (!prevBlank) lines.push_back(std::string());
            prevBlank = true;
        } else {
            prevBlank = false;
            while (para.size() > cols) {
                size_t cut = para.rfind(' ', cols);
                if (cut == std::string::npos || cut == 0) cut = cols;
                lines.push_back(para.substr(0, cut));
                size_t nx = para.find_first_not_of(' ', cut);
                para = (nx == std::string::npos) ? std::string() : para.substr(nx);
            }
            if (!para.empty()) lines.push_back(para);
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

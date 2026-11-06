#include "htmldoc.h"

#include <cctype>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------- UTF-8 -> ASCII
static std::string cpToAscii(uint32_t cp) {
    static const char* latin1[4] = {"AAAAAAACEEEEIIII", "DNOOOOOxOUUUUYTs", "aaaaaaaceeeeiiii", "dnooooo/ouuuuyty"};
    static const char* latinExtA =
        "AaAaAaCcCcCcCcDd" "DdEeEeEeEeEeGgGg" "GgGgHhHhIiIiIiIi" "IiIiJjKkkLlLlLlL"
        "lLlNnNnNnnNnOoOo" "OoOoRrRrRrSsSsSs" "SsTtTtTtUuUuUuUu" "UuUuWwYyYZzZzZzs";
    if (cp < 128) {
        if (cp >= 32 && cp < 127) return std::string(1, static_cast<char>(cp));
        if (cp == '\t' || cp == '\n' || cp == '\r') return " ";
        return std::string();
    }
    if (cp >= 0xC0 && cp <= 0xFF) return std::string(1, latin1[(cp - 0xC0) >> 4][(cp - 0xC0) & 15]);
    if (cp >= 0x100 && cp <= 0x17F) return std::string(1, latinExtA[cp - 0x100]);
    switch (cp) {
        case 0xA0: return " ";
        case 0xA1: return "!";
        case 0xA9: return "(c)";
        case 0xAB: return "<<";
        case 0xAD: return "";
        case 0xAE: return "(R)";
        case 0xB0: return "o";
        case 0xB7: return ".";
        case 0xBB: return ">>";
        case 0xBF: return "?";
        case 0x2002: case 0x2003: case 0x2009: case 0x202F: return " ";
        case 0x200B: case 0x200C: case 0x200D: case 0x200E: case 0x200F: case 0xFEFF: return "";
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: return "-";
        case 0x2018: case 0x2019: case 0x201A: case 0x2032: return "'";
        case 0x201C: case 0x201D: case 0x201E: case 0x2033: return "\"";
        case 0x2022: case 0x25CF: return "*";
        case 0x2026: return "...";
        case 0x20AC: return "EUR";
        case 0x2122: return "TM";
        case 0x2190: return "<-";
        case 0x2192: return "->";
        default: return "?";
    }
}

std::string foldToAscii(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp;
        int extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { i++; out += '?'; continue; }
        if (i + extra >= n) { out += '?'; break; }  // secuencia cortada al final
        for (int k = 1; k <= extra; k++) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += extra + 1;
        out += cpToAscii(cp);
    }
    return out;
}

// ---------------------------------------------------------------- entidades
static void appendUtf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) o += static_cast<char>(cp);
    else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

static std::string decodeEntities(const std::string& s) {
    if (s.find('&') == std::string::npos) return s;
    static const struct { const char* name; uint32_t cp; } named[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0}, {"copy", 0xA9}, {"reg", 0xAE},
        {"hellip", 0x2026}, {"mdash", 0x2014}, {"ndash", 0x2013}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"lsquo", 0x2018},
        {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bull", 0x2022}, {"middot", 0xB7}, {"times", 0xD7},
        {"euro", 0x20AC}, {"deg", 0xB0}, {"trade", 0x2122}, {"larr", 0x2190}, {"rarr", 0x2192}, {"iexcl", 0xA1}, {"iquest", 0xBF},
        {"aacute", 0xE1}, {"eacute", 0xE9}, {"iacute", 0xED}, {"oacute", 0xF3}, {"uacute", 0xFA}, {"ntilde", 0xF1}, {"uuml", 0xFC},
        {"Aacute", 0xC1}, {"Eacute", 0xC9}, {"Iacute", 0xCD}, {"Oacute", 0xD3}, {"Uacute", 0xDA}, {"Ntilde", 0xD1}};
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '&') { out += s[i]; continue; }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { out += '&'; continue; }
        std::string name = s.substr(i + 1, semi - i - 1);
        uint32_t cp = 0;
        bool ok = false;
        if (!name.empty() && name[0] == '#') {
            const char* p = name.c_str() + 1;
            char* end = nullptr;
            unsigned long v = (*p == 'x' || *p == 'X') ? std::strtoul(p + 1, &end, 16) : std::strtoul(p, &end, 10);
            if (end && *end == 0 && v > 0 && v < 0x110000) { cp = static_cast<uint32_t>(v); ok = true; }
        } else {
            for (const auto& e : named)
                if (name == e.name) { cp = e.cp; ok = true; break; }
        }
        if (ok) { appendUtf8(out, cp); i = semi; }
        else out += '&';
    }
    return out;
}

// ---------------------------------------------------------------- utilidades de analisis
static std::string lowerStr(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Busca 'needle' (minusculas) sin distinguir mayusculas a partir de 'from'.
static size_t findCI(const std::string& h, const char* needle, size_t from) {
    size_t nl = std::strlen(needle);
    for (size_t i = from; i + nl <= h.size(); i++) {
        i = h.find('<', i);
        if (i == std::string::npos || i + nl > h.size()) return std::string::npos;
        size_t k = 0;
        while (k < nl && std::tolower(static_cast<unsigned char>(h[i + k])) == needle[k]) k++;
        if (k == nl) return i;
    }
    return std::string::npos;
}

// Valor del atributo 'name' dentro del texto de una etiqueta (entre < y >). Vacio si no existe.
static bool getAttr(const std::string& tag, const char* name, std::string& val) {
    size_t nl = std::strlen(name);
    size_t i = 0, n = tag.size();
    while (i < n) {
        while (i < n && (std::isspace(static_cast<unsigned char>(tag[i])) || tag[i] == '/')) i++;
        size_t ks = i;
        while (i < n && !std::isspace(static_cast<unsigned char>(tag[i])) && tag[i] != '=' && tag[i] != '/' && tag[i] != '>') i++;
        std::string key = lowerStr(tag.substr(ks, i - ks));
        while (i < n && std::isspace(static_cast<unsigned char>(tag[i]))) i++;
        std::string v;
        if (i < n && tag[i] == '=') {
            i++;
            while (i < n && std::isspace(static_cast<unsigned char>(tag[i]))) i++;
            if (i < n && (tag[i] == '"' || tag[i] == '\'')) {
                char q = tag[i++];
                size_t vs = i;
                while (i < n && tag[i] != q) i++;
                v = tag.substr(vs, i - vs);
                if (i < n) i++;
            } else {
                size_t vs = i;
                while (i < n && !std::isspace(static_cast<unsigned char>(tag[i]))) i++;
                v = tag.substr(vs, i - vs);
            }
        }
        if (key.size() == nl && key == name) { val = decodeEntities(v); return true; }
        if (key.empty() && i < n) i++;
    }
    return false;
}

// ---------------------------------------------------------------- constructor de documento
namespace {

const size_t kMaxLines = 30000;

struct Builder {
    Document doc;
    size_t cols;
    DocLine cur;
    int col = 0;
    int indent = 0;
    int forcedStartCol = -1;
    int wantBlank = 0;
    bool pendingSpace = false;
    bool pendingSep = false;  // separador "|" de celdas, se emite al llegar la primera palabra de la celda

    explicit Builder(size_t c) : cols(c) {}

    bool full() const { return doc.lines.size() >= kMaxLines; }

    void newline() {
        if (!cur.spans.empty()) {
            if (!full()) doc.lines.push_back(cur);
            cur = DocLine();
        }
        col = indent;
        pendingSpace = false;
        pendingSep = false;
    }
    void block(int n) {  // n=1: salto de linea; n=2: parrafo (linea en blanco)
        newline();
        if (n - 1 > wantBlank) wantBlank = n - 1;
    }
    void startLineIfEmpty() {
        if (!cur.spans.empty()) return;
        if (wantBlank > 0 && !doc.lines.empty()) {
            for (int i = 0; i < wantBlank && !full(); i++) doc.lines.push_back(DocLine());
        }
        wantBlank = 0;
        col = (forcedStartCol >= 0) ? forcedStartCol : indent;
        forcedStartCol = -1;
    }
    void putSpan(const std::string& text, uint8_t style, int link, bool space) {
        int gap = (space && !cur.spans.empty()) ? 1 : 0;
        if (!cur.spans.empty() && gap == 1 && cur.spans.back().style == style && cur.spans.back().link == link) {
            cur.spans.back().text += " " + text;
        } else if (!cur.spans.empty() && gap == 0 && cur.spans.back().style == style && cur.spans.back().link == link &&
                   cur.spans.back().col + static_cast<int>(cur.spans.back().text.size()) == col) {
            cur.spans.back().text += text;
        } else {
            Span sp;
            sp.col = col + gap;
            sp.text = text;
            sp.style = style;
            sp.link = link;
            cur.spans.push_back(sp);
        }
        col += gap + static_cast<int>(text.size());
        if (link >= 0 && doc.linkLine[link] < 0) doc.linkLine[link] = static_cast<int>(doc.lines.size());
    }
    void addWord(const std::string& w, uint8_t style, int link) {
        if (w.empty() || full()) return;
        if (pendingSep) {
            pendingSep = false;
            if (!cur.spans.empty()) { pendingSpace = true; addWord("|", ST_DIM, -1); pendingSpace = true; }
        }
        startLineIfEmpty();
        int avail = static_cast<int>(cols) - indent;
        if (avail < 8) avail = 8;
        size_t pos = 0;
        while (pos < w.size()) {
            int gap = (pendingSpace && !cur.spans.empty()) ? 1 : 0;
            int remaining = static_cast<int>(w.size() - pos);
            if (col + gap + remaining > static_cast<int>(cols) && !cur.spans.empty()) {
                newline();
                startLineIfEmpty();
                continue;
            }
            int room = static_cast<int>(cols) - col - gap;
            if (room < 1) room = 1;
            int take = remaining < room ? remaining : room;
            putSpan(w.substr(pos, take), style, link, gap == 1);
            pendingSpace = false;
            pos += take;
            if (pos < w.size()) newline();
        }
    }
    void addText(const std::string& raw, uint8_t style, int link) {
        std::string t = foldToAscii(decodeEntities(raw));
        size_t i = 0, n = t.size();
        while (i < n) {
            if (t[i] == ' ') { pendingSpace = true; i++; continue; }
            size_t s = i;
            while (i < n && t[i] != ' ') i++;
            addWord(t.substr(s, i - s), style, link);
        }
    }
    void addPre(const std::string& raw) {
        // foldToAscii convierte \n en espacio; para <pre> se parte el texto decodificado por lineas antes de plegar.
        std::string dec = decodeEntities(raw);
        for (size_t p = dec.find('\t'); p != std::string::npos; p = dec.find('\t', p + 4)) dec.replace(p, 1, "    ");
        size_t start = 0;
        while (start <= dec.size() && !full()) {
            size_t nl = dec.find('\n', start);
            std::string line = foldToAscii(dec.substr(start, (nl == std::string::npos ? dec.size() : nl) - start));
            if (!line.empty() || nl != std::string::npos) {
                startLineIfEmpty();
                size_t p = 0;
                do {
                    std::string part = line.substr(p, cols - indent > 8 ? cols - indent : 8);
                    if (!part.empty()) putSpan(part, ST_PRE, -1, false);
                    p += part.size();
                    if (p < line.size()) newline();
                } while (p < line.size());
            }
            if (nl == std::string::npos) break;
            newline();
            start = nl + 1;
        }
    }
};

bool isSkipTag(const std::string& n) {
    return n == "script" || n == "style" || n == "noscript" || n == "svg" || n == "template" || n == "object" ||
           n == "select" || n == "textarea" || n == "canvas" || n == "audio";
}
bool isBlock1(const std::string& n) {
    static const char* b[] = {"div", "section", "article", "header", "footer", "nav", "main", "aside", "form", "fieldset", "figure",
                              "figcaption", "table", "tr", "ul", "ol", "dl", "dt", "dd", "address", "caption", "details", "summary",
                              "center", "tbody", "thead", "tfoot", "br", "hr"};
    for (const char* x : b) if (n == x) return true;
    return false;
}
bool isHeading(const std::string& n) { return n.size() == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6'; }

}  // namespace

Document parseHtml(const std::string& html, size_t cols) {
    Builder b(cols);
    bool inHead = false, inTitle = false;
    int boldDepth = 0, headingDepth = 0, preDepth = 0, quoteDepth = 0, currentLink = -1;
    std::vector<int> listCounters;  // -1 = lista con vinetas, >=0 = numerada (siguiente numero)
    int cellCount = 0;
    std::string titleBuf;

    size_t i = 0, n = html.size();
    while (i < n && !b.full()) {
        if (html[i] != '<') {
            size_t e = html.find('<', i);
            if (e == std::string::npos) e = n;
            std::string text = html.substr(i, e - i);
            i = e;
            if (inTitle) { titleBuf += text; continue; }
            if (inHead) continue;
            if (preDepth > 0) { b.addPre(text); continue; }
            uint8_t st = currentLink >= 0 ? ST_LINK : (headingDepth > 0 ? ST_HEADING : (boldDepth > 0 ? ST_BOLD : ST_NORMAL));
            b.addText(text, st, currentLink);
            continue;
        }
        // comentario / doctype / CDATA
        if (html.compare(i, 4, "<!--") == 0) {
            size_t e = html.find("-->", i + 4);
            i = (e == std::string::npos) ? n : e + 3;
            continue;
        }
        if (i + 1 < n && (html[i + 1] == '!' || html[i + 1] == '?')) {
            size_t e = html.find('>', i);
            i = (e == std::string::npos) ? n : e + 1;
            continue;
        }
        // Fin de etiqueta respetando comillas.
        size_t e = i + 1;
        char q = 0;
        while (e < n) {
            char c = html[e];
            if (q) { if (c == q) q = 0; }
            else if (c == '"' || c == '\'') q = c;
            else if (c == '>') break;
            e++;
        }
        if (e >= n) break;
        std::string tag = html.substr(i + 1, e - i - 1);
        i = e + 1;
        bool closing = !tag.empty() && tag[0] == '/';
        size_t k = closing ? 1 : 0, s = k;
        while (k < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[k])) || tag[k] == '-' || tag[k] == ':')) k++;
        std::string name = lowerStr(tag.substr(s, k - s));
        if (name.empty()) continue;
        std::string attrs = tag.substr(k);

        if (!inHead && !closing && (name == "iframe" || name == "video" || name == "source" || name == "embed")) {
            std::string src, type;
            if (!getAttr(attrs, "src", src)) getAttr(attrs, "data-src", src);
            getAttr(attrs, "type", type);
            if (!src.empty() && (name != "source" || true)) {
                std::string lt = lowerStr(type), ls = lowerStr(src);
                bool isVid = name == "video" || lt.find("video") != std::string::npos || ls.find(".mp4") != std::string::npos ||
                             ls.find(".webm") != std::string::npos || ls.find(".m3u8") != std::string::npos;
                if (name == "iframe" || name == "embed") {
                    b.doc.links.push_back(src);
                    b.doc.linkLine.push_back(-1);
                    b.block(1);
                    b.addWord(isVid || ls.find("youtube") != std::string::npos || ls.find("player") != std::string::npos ? "[video incrustado]" : "[marco incrustado]", ST_LINK, static_cast<int>(b.doc.links.size()) - 1);
                    b.block(1);
                } else if (isVid) {
                    b.doc.links.push_back(src);
                    b.doc.linkLine.push_back(-1);
                    b.block(1);
                    b.addWord("[video]", ST_LINK, static_cast<int>(b.doc.links.size()) - 1);
                    b.block(1);
                }
            }
            if (name == "iframe") {  // el contenido del iframe no es HTML de la pagina
                size_t z = findCI(html, "</iframe", i);
                if (z != std::string::npos) { size_t z2 = html.find('>', z); i = (z2 == std::string::npos) ? n : z2 + 1; }
            }
            continue;
        }
        if (inHead && !closing && name == "meta") {
            std::string he, ct;
            if (getAttr(attrs, "http-equiv", he) && lowerStr(he) == "refresh" && getAttr(attrs, "content", ct)) {
                size_t u = lowerStr(ct).find("url=");
                if (u != std::string::npos) {
                    std::string r = ct.substr(u + 4);
                    while (!r.empty() && (r[0] == ' ' || r[0] == '\'' || r[0] == '"')) r.erase(0, 1);
                    while (!r.empty() && (r.back() == ' ' || r.back() == '\'' || r.back() == '"')) r.pop_back();
                    b.doc.refreshUrl = r;
                }
            }
            continue;
        }
        if (!closing && isSkipTag(name)) {
            std::string endTag = "</" + name;
            size_t z = findCI(html, endTag.c_str(), i);
            if (z == std::string::npos) { i = n; break; }
            size_t z2 = html.find('>', z);
            i = (z2 == std::string::npos) ? n : z2 + 1;
            continue;
        }

        if (name == "head") { inHead = !closing; continue; }
        if (name == "body") { inHead = false; continue; }
        if (name == "title") {
            if (!closing) { inTitle = true; titleBuf.clear(); }
            else { inTitle = false; b.doc.title = foldToAscii(decodeEntities(titleBuf)); }
            continue;
        }
        if (inHead) continue;

        if (name == "a") {
            if (!closing) {
                std::string href;
                if (getAttr(attrs, "href", href) && !href.empty()) {
                    b.doc.links.push_back(href);
                    b.doc.linkLine.push_back(-1);
                    currentLink = static_cast<int>(b.doc.links.size()) - 1;
                }
            } else {
                currentLink = -1;
            }
            continue;
        }
        if (name == "b" || name == "strong" || name == "th") {
            boldDepth += closing ? (boldDepth > 0 ? -1 : 0) : 1;
            if (name == "th") {
                if (!closing && cellCount++ > 0) b.pendingSep = true;
                b.pendingSpace = true;
            }
            continue;
        }
        if (name == "td") {
            if (!closing && cellCount++ > 0) b.pendingSep = true;
            b.pendingSpace = true;
            continue;
        }
        if (name == "tr") { cellCount = 0; b.block(1); continue; }
        if (isHeading(name)) {
            if (!closing) { b.block(2); headingDepth++; }
            else { if (headingDepth > 0) headingDepth--; b.block(2); }
            continue;
        }
        if (name == "p") { b.block(2); continue; }
        if (name == "pre") {
            if (!closing) { b.block(2); preDepth++; }
            else { if (preDepth > 0) preDepth--; b.block(2); }
            continue;
        }
        if (name == "blockquote") {
            b.block(2);
            quoteDepth += closing ? (quoteDepth > 0 ? -1 : 0) : 1;
            b.indent = 2 * quoteDepth + 2 * static_cast<int>(listCounters.size());
            b.col = b.indent;
            continue;
        }
        if (name == "ul" || name == "ol") {
            b.block(1);
            if (!closing) listCounters.push_back(name == "ol" ? 1 : -1);
            else if (!listCounters.empty()) listCounters.pop_back();
            b.indent = 2 * quoteDepth + 2 * static_cast<int>(listCounters.size());
            b.col = b.indent;
            continue;
        }
        if (name == "li") {
            if (!closing) {
                b.block(1);
                std::string bullet = "*";
                if (!listCounters.empty() && listCounters.back() >= 0) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%d.", listCounters.back()++);
                    bullet = buf;
                }
                int start = b.indent - 2;
                b.forcedStartCol = start < 0 ? 0 : start;
                b.addWord(bullet, ST_DIM, -1);
                b.pendingSpace = true;
            }
            continue;
        }
        if (name == "img") {
            if (!closing) {
                std::string alt;
                getAttr(attrs, "alt", alt);
                alt = foldToAscii(alt);
                if (!alt.empty()) b.addWord("[img: " + alt + "]", currentLink >= 0 ? ST_LINK : ST_DIM, currentLink);
                else if (currentLink >= 0) b.addWord("[img]", ST_LINK, currentLink);
            }
            continue;
        }
        if (name == "hr") {
            b.block(1);
            b.startLineIfEmpty();
            b.putSpan(std::string(cols > 60 ? 60 : cols, '-'), ST_DIM, -1, false);
            b.block(1);
            continue;
        }
        if (isBlock1(name)) { b.block(1); continue; }
        // resto de etiquetas (span, em, i, code, small, font...): en linea, sin efecto
    }
    b.newline();
    return b.doc;
}

Document parsePlain(const std::string& text, size_t cols) {
    Document d;
    size_t start = 0;
    while (start <= text.size() && d.lines.size() < kMaxLines) {
        size_t nl = text.find('\n', start);
        std::string line = foldToAscii(text.substr(start, (nl == std::string::npos ? text.size() : nl) - start));
        size_t p = 0;
        do {
            DocLine dl;
            std::string part = line.substr(p, cols);
            if (!part.empty()) {
                Span sp;
                sp.col = 0;
                sp.text = part;
                sp.style = ST_PRE;
                dl.spans.push_back(sp);
            }
            d.lines.push_back(dl);
            p += part.size();
        } while (p < line.size());
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return d;
}

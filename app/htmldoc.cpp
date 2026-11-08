#include "htmldoc.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "textengine.h"

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


// ---------------------------------------------------------------- colores CSS
uint32_t parseCssColor(const std::string& in) {
    std::string s = lowerStr(in);
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    s = s.substr(a);
    while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) || s.back() == ';')) s.pop_back();
    if (s.empty()) return 0;
    if (s[0] == '#') {
        std::string h = s.substr(1);
        for (char c : h) if (!std::isxdigit(static_cast<unsigned char>(c))) return 0;
        if (h.size() == 3) h = std::string() + h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
        if (h.size() != 6) return 0;
        return 0xFF000000u | static_cast<uint32_t>(std::strtoul(h.c_str(), nullptr, 16));
    }
    if (s.compare(0, 4, "rgb(") == 0 || s.compare(0, 5, "rgba(") == 0) {
        int v[3] = {0, 0, 0};
        const char* p = std::strchr(s.c_str(), '(') + 1;
        for (int k = 0; k < 3; k++) {
            char* e = nullptr;
            v[k] = static_cast<int>(std::strtol(p, &e, 10));
            if (e == p) return 0;
            p = e;
            while (*p == ' ' || *p == ',' || *p == '/') p++;
        }
        for (int k = 0; k < 3; k++) v[k] = std::max(0, std::min(255, v[k]));
        return 0xFF000000u | (v[0] << 16) | (v[1] << 8) | v[2];
    }
    static const struct { const char* n; uint32_t c; } names[] = {
        {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000}, {"green", 0x008000}, {"blue", 0x0000FF}, {"gray", 0x808080},
        {"grey", 0x808080}, {"orange", 0xFFA500}, {"yellow", 0xFFFF00}, {"purple", 0x800080}, {"navy", 0x000080}, {"maroon", 0x800000},
        {"teal", 0x008080}, {"silver", 0xC0C0C0}, {"lime", 0x00FF00}, {"aqua", 0x00FFFF}, {"fuchsia", 0xFF00FF}, {"brown", 0xA52A2A}};
    for (const auto& e : names) if (s == e.n) return 0xFF000000u | e.c;
    return 0;
}

static std::string styleProp(const std::string& style, const char* prop) {  // "color: red; x:y" -> "red"
    std::string st = lowerStr(style);
    size_t pl = std::strlen(prop);
    size_t p = 0;
    while ((p = st.find(prop, p)) != std::string::npos) {
        bool startOk = p == 0 || st[p - 1] == ';' || std::isspace(static_cast<unsigned char>(st[p - 1]));
        size_t q = p + pl;
        while (q < st.size() && std::isspace(static_cast<unsigned char>(st[q]))) q++;
        if (startOk && q < st.size() && st[q] == ':') {
            size_t e = st.find(';', q);
            return st.substr(q + 1, (e == std::string::npos ? st.size() : e) - q - 1);
        }
        p += pl;
    }
    return std::string();
}

// ---------------------------------------------------------------- constructor de documento
namespace {

const size_t kMaxLines = 40000;
const int kMaxImages = 60;

struct Pen {
    uint8_t font = FONT_SANS;
    int size = 16;
    bool bold = false;
    uint8_t role = ROLE_TEXT;
    uint32_t color = 0;
    int link = -1;
    int focus = -1;
};

struct Builder {
    Document doc;
    int W;
    DocLine cur;
    int x = 0, indent = 0, curY = 0, pendingGap = 0, forcedStartX = -1, lineTop = 0;
    int maxAsc = 0, maxDesc = 0, maxSize = 0;
    bool pendingSpace = false, pendingSep = false;
    int extraGap = 0;  // hueco horizontal extra (separacion entre celdas de tabla)

    explicit Builder(int w) : W(w) { doc.width = w; }

    bool full() const { return doc.lines.size() >= kMaxLines; }

    void startLineIfEmpty() {
        if (!cur.spans.empty()) return;
        lineTop = (doc.lines.empty() && curY == 0) ? 0 : curY + pendingGap;
        pendingGap = 0;
        x = forcedStartX >= 0 ? forcedStartX : indent;
        forcedStartX = -1;
        maxAsc = maxDesc = maxSize = 0;
    }
    void note(int asc, int desc, int size) {
        maxAsc = std::max(maxAsc, asc);
        maxDesc = std::max(maxDesc, desc);
        maxSize = std::max(maxSize, size);
    }
    void newline() {
        if (!cur.spans.empty()) {
            const int lead = static_cast<int>(std::lround(maxSize * 0.30));
            cur.y = lineTop;
            cur.baseline = lead / 2 + maxAsc;
            cur.h = maxAsc + maxDesc + lead;
            curY = lineTop + cur.h;
            if (!full()) doc.lines.push_back(std::move(cur));
            cur = DocLine();
        }
        x = indent;
        pendingSpace = false;
        pendingSep = false;
    }
    void block(int gapPx) {  // salto de linea; gapPx > 0 deja ademas un hueco vertical antes del siguiente bloque
        newline();
        if (gapPx > pendingGap) pendingGap = gapPx;
    }
    void touchFocus(int focus) {
        if (focus >= 0 && doc.focus[focus].line < 0) doc.focus[focus].line = static_cast<int>(doc.lines.size());
    }
    void putText(const std::string& text, const Pen& p, int w, int gap) {
        if (!cur.spans.empty()) {
            Span& l = cur.spans.back();
            if (l.kind == SK_TEXT && l.font == p.font && l.size == p.size && l.bold == p.bold && l.role == p.role && l.color == p.color &&
                l.link == p.link && l.focus == p.focus) {
                l.text += gap > 0 ? " " + text : text;
                l.w = te_width(l.text, l.font, l.size, l.bold);  // medir el span entero evita acumular redondeos
                x = l.x + l.w;
                touchFocus(p.focus);
                return;
            }
        }
        Span s;
        s.x = x + gap;
        s.w = w;
        s.kind = SK_TEXT;
        s.font = p.font; s.size = p.size; s.bold = p.bold; s.role = p.role; s.color = p.color;
        s.link = p.link; s.focus = p.focus;
        s.text = text;
        cur.spans.push_back(std::move(s));
        x += gap + w;
        note(te_ascent(p.font, p.size), te_descent(p.font, p.size), p.size);
        touchFocus(p.focus);
    }
    int spaceW(const Pen& p) { return te_width(" ", p.font, p.size, p.bold); }

    void addWord(const std::string& w, const Pen& p) {
        if (w.empty() || full()) return;
        if (pendingSep) {
            pendingSep = false;
            if (!cur.spans.empty()) {
                Pen d = p;
                d.role = ROLE_DIM; d.link = -1; d.focus = -1; d.bold = false; d.color = 0;
                pendingSpace = true;
                addWord("|", d);
                pendingSpace = true;
            }
        }
        startLineIfEmpty();
        int gap = (pendingSpace && !cur.spans.empty()) ? spaceW(p) : 0;
        if (extraGap > 0) { if (!cur.spans.empty()) gap += extraGap; extraGap = 0; }
        const int ww = te_width(w, p.font, p.size, p.bold);
        if (x + gap + ww > W && !cur.spans.empty()) {
            newline();
            startLineIfEmpty();
            gap = 0;
        }
        if (x + ww > W) {  // palabra mas ancha que la linea: partirla por caracteres
            size_t pos = 0;
            while (pos < w.size()) {
                size_t i = pos, lastOk = pos;
                while (i < w.size()) {
                    size_t j = i;
                    te_next(w, j);
                    if (te_width(w.substr(pos, j - pos), p.font, p.size, p.bold) > W - x && lastOk > pos) break;
                    lastOk = i = j;
                }
                if (lastOk == pos) { size_t j = pos; te_next(w, j); lastOk = j; }
                const std::string part = w.substr(pos, lastOk - pos);
                putText(part, p, te_width(part, p.font, p.size, p.bold), 0);
                pos = lastOk;
                if (pos < w.size()) { newline(); startLineIfEmpty(); }
            }
        } else {
            putText(w, p, ww, gap);
        }
        pendingSpace = false;
    }
    void addText(const std::string& raw, const Pen& p) {
        const std::string t = decodeEntities(raw);
        size_t i = 0, n = t.size();
        while (i < n) {
            const unsigned char c = static_cast<unsigned char>(t[i]);
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f') { pendingSpace = true; i++; continue; }
            if (c == 0xC2 && i + 1 < n && static_cast<unsigned char>(t[i + 1]) == 0xA0) { pendingSpace = true; i += 2; continue; }
            size_t s = i;
            while (i < n) {
                const unsigned char d = static_cast<unsigned char>(t[i]);
                if (d == ' ' || d == '\n' || d == '\r' || d == '\t' || d == '\f') break;
                if (d == 0xC2 && i + 1 < n && static_cast<unsigned char>(t[i + 1]) == 0xA0) break;
                i++;
            }
            addWord(t.substr(s, i - s), p);
        }
    }
    void addPre(const std::string& raw, const Pen& p) {
        std::string dec = decodeEntities(raw);
        for (size_t q = dec.find('\t'); q != std::string::npos; q = dec.find('\t', q + 4)) dec.replace(q, 1, "    ");
        const int cw = std::max(1, te_width("M", FONT_MONO, p.size, false));
        size_t start = 0;
        while (start <= dec.size() && !full()) {
            size_t nl = dec.find('\n', start);
            std::string line = dec.substr(start, (nl == std::string::npos ? dec.size() : nl) - start);
            while (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() || nl != std::string::npos) {
                startLineIfEmpty();
                if (line.empty()) { putText(" ", p, cw, 0); }
                size_t q = 0;
                while (q < line.size()) {
                    const size_t maxChars = static_cast<size_t>(std::max(8, (W - x) / cw));
                    size_t e = q, cnt = 0;
                    while (e < line.size() && cnt < maxChars) { te_next(line, e); cnt++; }
                    putText(line.substr(q, e - q), p, static_cast<int>(cnt) * cw, 0);
                    q = e;
                    if (q < line.size()) { newline(); startLineIfEmpty(); }
                }
            }
            if (nl == std::string::npos) break;
            newline();
            start = nl + 1;
        }
    }
    // Controles (imagenes, campos, botones, casillas) y reglas horizontales.
    void addBox(SpanKind kind, int w, int h, int ref, const Pen& p, const std::string& text = std::string()) {
        if (full()) return;
        startLineIfEmpty();
        int gap = (pendingSpace && !cur.spans.empty()) ? spaceW(p) : 0;
        if (extraGap > 0) { if (!cur.spans.empty()) gap += extraGap; extraGap = 0; }
        if (x + gap + w > W && !cur.spans.empty()) {
            newline();
            startLineIfEmpty();
            gap = 0;
        }
        Span s;
        s.x = x + gap; s.w = w; s.h = h; s.kind = kind; s.ref = ref; s.text = text;
        s.font = p.font; s.size = p.size; s.bold = p.bold; s.role = p.role; s.color = p.color; s.link = p.link; s.focus = p.focus;
        cur.spans.push_back(std::move(s));
        x += gap + w;
        if (kind == SK_IMAGE) note(h, 0, p.size);
        else if (kind == SK_RULE) note(6, 6, 12);
        else note(h - h / 4, h / 4, p.size);
        touchFocus(p.focus);
        pendingSpace = true;
    }
};

bool isSkipTag(const std::string& n) {
    return n == "script" || n == "style" || n == "svg" || n == "template" || n == "object" || n == "canvas" ||
           n == "audio" || n == "map" || n == "datalist" || n == "dialog";
}
bool isVoid(const std::string& n) {
    static const char* v[] = {"br", "img", "input", "meta", "link", "hr", "source", "area", "base", "col", "embed", "param", "track", "wbr"};
    for (const char* x : v) if (n == x) return true;
    return false;
}
bool isBlock1(const std::string& n) {
    static const char* b[] = {"div", "section", "article", "header", "footer", "nav", "main", "aside", "form", "fieldset", "figure",
                              "figcaption", "table", "tr", "dl", "dt", "dd", "address", "caption", "details", "summary", "center",
                              "tbody", "thead", "tfoot", "li"};
    for (const char* x : b) if (n == x) return true;
    return false;
}
bool isHeading(const std::string& n) { return n.size() == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6'; }

// Fin de etiqueta ('>') respetando comillas. Devuelve npos si no hay.
size_t tagEnd(const std::string& h, size_t i) {
    char q = 0;
    for (size_t e = i; e < h.size(); e++) {
        char c = h[e];
        if (q) { if (c == q) q = 0; }
        else if (c == '"' || c == '\'') q = c;
        else if (c == '>') return e;
    }
    return std::string::npos;
}

// 'i' apunta justo despues del '>' de la etiqueta de apertura de <name>; devuelve la posicion tras su cierre.
size_t skipSubtree(const std::string& h, size_t i, const std::string& name) {
    int depth = 1;
    const size_t n = h.size();
    while (i < n) {
        size_t lt = h.find('<', i);
        if (lt == std::string::npos) return n;
        if (h.compare(lt, 4, "<!--") == 0) { size_t e = h.find("-->", lt + 4); i = e == std::string::npos ? n : e + 3; continue; }
        const bool close = lt + 1 < n && h[lt + 1] == '/';
        size_t s = lt + (close ? 2 : 1), k = s;
        while (k < n && (std::isalnum(static_cast<unsigned char>(h[k])) || h[k] == '-' || h[k] == ':')) k++;
        const std::string nm = lowerStr(h.substr(s, k - s));
        size_t gt = tagEnd(h, k);
        if (gt == std::string::npos) return n;
        if (nm == name) {
            if (close) { if (--depth == 0) return gt + 1; }
            else if (h[gt - 1] != '/') depth++;
        }
        i = gt + 1;
    }
    return n;
}

std::string stripTags(const std::string& s) {
    std::string o;
    bool in = false;
    for (char c : s) {
        if (c == '<') in = true;
        else if (c == '>') in = false;
        else if (!in) o += c;
    }
    return o;
}
std::string collapseWs(const std::string& s) {
    std::string o;
    bool sp = false;
    for (unsigned char c : s) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') { sp = true; continue; }
        if (sp && !o.empty()) o += ' ';
        sp = false;
        o += static_cast<char>(c);
    }
    return o;
}
int attrInt(const std::string& attrs, const char* name) {
    std::string v;
    if (!getAttr(attrs, name, v)) return 0;
    if (v.find('%') != std::string::npos) return 0;
    return std::atoi(v.c_str());
}

}  // namespace

Document parseHtml(const std::string& html, int width, int zoomPct) {
    Builder b(width);
    const int baseSize = std::max(10, static_cast<int>(std::lround(16 * zoomPct / 100.0)));
    auto sc = [&](int v) { return std::max(1, static_cast<int>(std::lround(v * zoomPct / 100.0))); };
    bool inHead = false, inTitle = false;
    int boldDepth = 0, preDepth = 0, quoteDepth = 0, codeDepth = 0, smallDepth = 0;
    std::vector<int> headStack;
    std::vector<std::pair<std::string, uint32_t>> colorStack;
    std::vector<int> listCounters;  // -1 = vinetas, >=0 = siguiente numero
    int cellCount = 0, currentLink = -1, currentFocus = -1, curForm = -1, inCell = 0;
    bool tableHasTh = false;
    std::string titleBuf;

    auto pen = [&]() {
        Pen p;
        p.size = baseSize;
        if (!headStack.empty()) {
            static const double hs[7] = {1, 2.0, 1.6, 1.3, 1.1, 1.0, 0.9};
            p.size = std::max(10, static_cast<int>(std::lround(baseSize * hs[headStack.back()])));
            p.bold = true;
            p.role = ROLE_HEAD;
        } else if (boldDepth > 0) p.bold = true;
        if (smallDepth > 0) p.size = std::max(9, p.size * 85 / 100);
        if (codeDepth > 0) { p.font = FONT_MONO; p.size = std::max(9, p.size * 92 / 100); p.role = ROLE_CODE; }
        if (!colorStack.empty()) p.color = colorStack.back().second;
        if (currentLink >= 0) { p.role = ROLE_LINK; p.link = currentLink; p.focus = currentFocus; }
        return p;
    };
    auto addAnchor = [&](const std::string& id) {
        if (!id.empty() && b.doc.anchors.size() < 3000) b.doc.anchors.push_back({id, static_cast<int>(b.doc.lines.size())});
    };

    size_t i = 0, n = html.size();
    while (i < n && !b.full()) {
        if (html[i] != '<') {
            size_t e = html.find('<', i);
            if (e == std::string::npos) e = n;
            const std::string text = html.substr(i, e - i);
            i = e;
            if (inTitle) { titleBuf += text; continue; }
            if (inHead) continue;
            if (preDepth > 0) {
                Pen p = pen();
                p.font = FONT_MONO;
                p.size = std::max(9, static_cast<int>(std::lround(baseSize * 0.9)));
                if (p.role == ROLE_TEXT) p.role = ROLE_CODE;
                b.addPre(text, p);
                continue;
            }
            b.addText(text, pen());
            continue;
        }
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
        const size_t e = tagEnd(html, i + 1);
        if (e == std::string::npos) break;
        const std::string tag = html.substr(i + 1, e - i - 1);
        i = e + 1;
        const bool closing = !tag.empty() && tag[0] == '/';
        size_t k = closing ? 1 : 0;
        const size_t s0 = k;
        while (k < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[k])) || tag[k] == '-' || tag[k] == ':')) k++;
        const std::string name = lowerStr(tag.substr(s0, k - s0));
        if (name.empty()) continue;
        const std::string attrs = tag.substr(k);

        if (!closing && isSkipTag(name)) {
            const size_t z = findCI(html, ("</" + name).c_str(), i);
            if (z == std::string::npos) { i = n; break; }
            const size_t z2 = html.find('>', z);
            i = (z2 == std::string::npos) ? n : z2 + 1;
            continue;
        }
        // Elementos ocultos (display:none / atributo hidden): se salta todo el subarbol.
        if (!closing && !isVoid(name) && name != "html" && name != "body" && name != "head" && name != "title" && !(inHead)) {
            std::string st, hid;
            bool hidden = false;
            if (getAttr(attrs, "style", st)) {
                std::string d = lowerStr(styleProp(st, "display"));
                d.erase(std::remove_if(d.begin(), d.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), d.end());
                hidden = d == "none";
            }
            if (!hidden && getAttr(attrs, "hidden", hid) && name != "input") hidden = true;
            if (hidden && tag.empty() == false && tag.back() != '/') { i = skipSubtree(html, i, name); continue; }
        }
        if (!closing) { std::string id; if (getAttr(attrs, "id", id)) addAnchor(id); }

        if (name == "head") { inHead = !closing; continue; }
        if (name == "body") { inHead = false; continue; }
        if (name == "title") {
            if (!closing) { inTitle = true; titleBuf.clear(); }
            else { inTitle = false; b.doc.title = collapseWs(decodeEntities(titleBuf)); }
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
        if (inHead) continue;

        // --- video / marcos incrustados: se convierten en enlaces
        if (!closing && (name == "iframe" || name == "video" || name == "source" || name == "embed")) {
            std::string src, type;
            if (!getAttr(attrs, "src", src)) getAttr(attrs, "data-src", src);
            getAttr(attrs, "type", type);
            if (!src.empty()) {
                const std::string lt = lowerStr(type), ls = lowerStr(src);
                const bool isVid = name == "video" || lt.find("video") != std::string::npos || ls.find(".mp4") != std::string::npos ||
                                   ls.find(".webm") != std::string::npos || ls.find(".m3u8") != std::string::npos;
                if (name == "iframe" || name == "embed" || isVid) {
                    b.doc.links.push_back(src);
                    b.doc.focus.push_back({0, static_cast<int>(b.doc.links.size()) - 1, -1});
                    Pen p;
                    p.size = baseSize; p.role = ROLE_LINK; p.link = static_cast<int>(b.doc.links.size()) - 1;
                    p.focus = static_cast<int>(b.doc.focus.size()) - 1;
                    b.block(0);
                    b.addWord(name == "iframe" || name == "embed" ? (isVid || ls.find("youtube") != std::string::npos ? "[video incrustado]" : "[marco incrustado]") : "[video]", p);
                    b.block(0);
                }
            }
            if (name == "iframe") {
                const size_t z = findCI(html, "</iframe", i);
                if (z != std::string::npos) { size_t z2 = html.find('>', z); i = (z2 == std::string::npos) ? n : z2 + 1; }
            }
            continue;
        }

        // --- enlaces
        if (name == "a") {
            if (!closing) {
                std::string href;
                if (getAttr(attrs, "href", href) && !href.empty()) {
                    b.doc.links.push_back(href);
                    b.doc.focus.push_back({0, static_cast<int>(b.doc.links.size()) - 1, -1});
                    currentLink = static_cast<int>(b.doc.links.size()) - 1;
                    currentFocus = static_cast<int>(b.doc.focus.size()) - 1;
                } else {
                    std::string nm;
                    if (getAttr(attrs, "name", nm)) addAnchor(nm);
                }
            } else {
                currentLink = -1;
                currentFocus = -1;
            }
            continue;
        }
        // --- color en linea
        if (name == "font" || name == "span") {
            if (!closing) {
                uint32_t col = 0;
                std::string v;
                if (name == "font" && getAttr(attrs, "color", v)) col = parseCssColor(v);
                if (getAttr(attrs, "style", v)) { uint32_t c2 = parseCssColor(styleProp(v, "color")); if (c2) col = c2; }
                colorStack.push_back({name, col ? col : (colorStack.empty() ? 0 : colorStack.back().second)});
            } else if (!colorStack.empty() && colorStack.back().first == name) {
                colorStack.pop_back();
            }
            continue;
        }
        if (name == "table") { if (!closing) tableHasTh = false; inCell = 0; b.block(0); continue; }
        if (name == "b" || name == "strong" || name == "th") {
            boldDepth += closing ? (boldDepth > 0 ? -1 : 0) : 1;
            if (name == "th") {
                if (!closing) {
                    tableHasTh = true;
                    inCell++;
                    if (cellCount++ > 0) b.pendingSep = true;
                    b.pendingSpace = true;
                } else if (inCell > 0) inCell--;
            }
            continue;
        }
        if (name == "small") { smallDepth += closing ? (smallDepth > 0 ? -1 : 0) : 1; continue; }
        if (name == "code" || name == "kbd" || name == "samp" || name == "tt") { codeDepth += closing ? (codeDepth > 0 ? -1 : 0) : 1; continue; }
        if (name == "td") {
            if (!closing) {
                inCell++;
                if (cellCount++ > 0) { if (tableHasTh) b.pendingSep = true; else b.extraGap = sc(14); }
                b.pendingSpace = true;
            } else if (inCell > 0) inCell--;
            continue;
        }
        if (name == "tr") { cellCount = 0; inCell = 0; b.block(0); continue; }
        if (isHeading(name)) {
            if (!closing) { b.block(sc(18)); headStack.push_back(name[1] - '0'); }
            else { if (!headStack.empty()) headStack.pop_back(); b.block(sc(10)); }
            continue;
        }
        if (name == "p") { b.block(sc(12)); continue; }
        if (name == "pre") {
            if (!closing) { b.block(sc(12)); preDepth++; }
            else { if (preDepth > 0) preDepth--; b.block(sc(12)); }
            continue;
        }
        if (name == "blockquote") {
            b.block(sc(12));
            quoteDepth += closing ? (quoteDepth > 0 ? -1 : 0) : 1;
            b.indent = sc(26) * (quoteDepth + static_cast<int>(listCounters.size()));
            continue;
        }
        if (name == "ul" || name == "ol") {
            b.block(listCounters.empty() ? sc(8) : 0);
            if (!closing) listCounters.push_back(name == "ol" ? 1 : -1);
            else if (!listCounters.empty()) listCounters.pop_back();
            b.indent = sc(26) * (quoteDepth + static_cast<int>(listCounters.size()));
            continue;
        }
        if (name == "li") {
            if (!closing) {
                b.block(sc(3));
                std::string bullet = "\xE2\x80\xA2";  // U+2022
                if (!listCounters.empty() && listCounters.back() >= 0) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%d.", listCounters.back()++);
                    bullet = buf;
                }
                Pen p;
                p.size = baseSize; p.role = ROLE_DIM;
                const int w = te_width(bullet, p.font, p.size, false);
                b.forcedStartX = std::max(0, b.indent - sc(10) - w);
                b.addWord(bullet, p);
                b.pendingSpace = false;
                b.x = std::max(b.x, b.indent);  // el texto empieza en la sangria de la lista
            }
            continue;
        }
        if (name == "br") {
            if (b.cur.spans.empty()) b.pendingGap += baseSize; else b.newline();
            continue;
        }
        if (name == "hr") {
            b.block(sc(8));
            Pen p; p.size = baseSize;
            b.addBox(SK_RULE, std::max(10, b.W - b.indent), 1, -1, p);
            b.block(sc(8));
            continue;
        }
        // --- imagenes
        if (name == "img") {
            if (closing) continue;
            std::string src, alt;
            // Imagenes "perezosas": el src real suele ir en data-src/data-original... y src es un marcador (data: o gif vacio).
            {
                getAttr(attrs, "src", src);
                const std::string ls = lowerStr(src);
                const bool placeholder = src.empty() || ls.compare(0, 5, "data:") == 0 || ls.find("blank") != std::string::npos ||
                                         ls.find("pixel") != std::string::npos || ls.find("spacer") != std::string::npos ||
                                         ls.find("placeholder") != std::string::npos || ls.find("lazy") != std::string::npos ||
                                         ls.find("transparent") != std::string::npos || ls.find("1x1") != std::string::npos;
                if (placeholder) {
                    static const char* lazy[] = {"data-src", "data-original", "data-lazy-src", "data-lazy", "data-url", "data-echo", "data-thumb"};
                    for (const char* a : lazy) {
                        std::string v;
                        if (getAttr(attrs, a, v) && !v.empty() && v.compare(0, 5, "data:") != 0) { src = v; break; }
                    }
                    if (src.empty() || src.compare(0, 5, "data:") == 0) {  // srcset / data-srcset: primera URL
                        std::string ss;
                        if ((getAttr(attrs, "data-srcset", ss) || getAttr(attrs, "srcset", ss)) && !ss.empty()) {
                            size_t b0 = ss.find_first_not_of(" \t\r\n"), e0 = ss.find_first_of(" \t\r\n,", b0 == std::string::npos ? 0 : b0);
                            if (b0 != std::string::npos) src = ss.substr(b0, e0 == std::string::npos ? std::string::npos : e0 - b0);
                        }
                    }
                }
            }
            getAttr(attrs, "alt", alt);
            alt = collapseWs(alt);
            int w = attrInt(attrs, "width"), h = attrInt(attrs, "height");
            if ((w > 0 && w <= 3) || (h > 0 && h <= 3)) continue;  // pixeles de seguimiento
            if (src.empty() || src.compare(0, 5, "data:") == 0 || static_cast<int>(b.doc.images.size()) >= kMaxImages) {
                if (!alt.empty()) { Pen p = pen(); p.role = currentLink >= 0 ? ROLE_LINK : ROLE_DIM; b.addText("[" + alt + "]", p); }
                continue;
            }
            if (w <= 0 && h <= 0) { w = 160; h = 100; }
            else if (w <= 0) w = h * 3 / 2;
            else if (h <= 0) h = w * 2 / 3;
            const int maxW = std::max(40, b.W - b.indent);
            if (w > maxW) { h = h * maxW / w; w = maxW; }
            if (h > 700) { w = w * 700 / h; h = 700; }
            ImageRef im;
            im.src = src; im.alt = alt; im.w = std::max(8, w); im.h = std::max(8, h);
            b.doc.images.push_back(std::move(im));
            b.addBox(SK_IMAGE, std::max(8, w), std::max(8, h), static_cast<int>(b.doc.images.size()) - 1, pen());
            continue;
        }
        // --- formularios
        if (name == "form") {
            if (!closing) {
                Form f;
                std::string v;
                getAttr(attrs, "action", f.action);
                f.post = getAttr(attrs, "method", v) && lowerStr(v) == "post";
                b.doc.forms.push_back(std::move(f));
                curForm = static_cast<int>(b.doc.forms.size()) - 1;
            } else curForm = -1;
            b.block(sc(4));
            continue;
        }
        if (name == "input" && !closing) {
            std::string type, nm, val, ph, sz, chk;
            getAttr(attrs, "type", type);
            getAttr(attrs, "name", nm);
            getAttr(attrs, "value", val);
            getAttr(attrs, "placeholder", ph);
            getAttr(attrs, "size", sz);
            type = lowerStr(type);
            FormField f;
            f.form = curForm; f.name = nm; f.value = collapseWs(val); f.placeholder = collapseWs(ph);
            f.checked = getAttr(attrs, "checked", chk);
            if (type == "hidden") f.type = "hidden";
            else if (type == "submit" || type == "button" || type == "image" || type == "reset") f.type = type == "reset" ? "reset" : "submit";
            else if (type == "checkbox" || type == "radio") f.type = type;
            else if (type == "password") f.type = "password";
            else f.type = "text";
            b.doc.fields.push_back(f);
            const int fi = static_cast<int>(b.doc.fields.size()) - 1;
            if (f.type == "hidden") continue;
            Pen p = pen();
            p.link = -1; p.focus = -1;
            b.doc.focus.push_back({1, fi, -1});
            p.focus = static_cast<int>(b.doc.focus.size()) - 1;
            if (f.type == "submit" || f.type == "reset") {
                std::string label = f.value.empty() ? (f.type == "reset" ? "Restablecer" : "Enviar") : f.value;
                b.addBox(SK_BUTTON, te_width(label, FONT_SANS, baseSize, false) + sc(28), sc(32), fi, p, label);
            } else if (f.type == "checkbox" || f.type == "radio") {
                b.addBox(SK_CHECK, sc(20), sc(20), fi, p);
            } else {
                int chars = std::atoi(sz.c_str());
                int w = chars > 0 ? sc(chars * 9 + 20) : sc(240);
                w = std::max(sc(110), std::min(w, std::min(sc(560), std::max(sc(110), b.W - b.indent))));
                b.addBox(SK_INPUT, w, sc(32), fi, p);
            }
            continue;
        }
        if (name == "textarea" && !closing) {
            const size_t z = findCI(html, "</textarea", i);
            const std::string inner = html.substr(i, z == std::string::npos ? n - i : z - i);
            if (z == std::string::npos) i = n; else { size_t z2 = html.find('>', z); i = z2 == std::string::npos ? n : z2 + 1; }
            FormField f;
            f.form = curForm; f.type = "text";
            getAttr(attrs, "name", f.name);
            getAttr(attrs, "placeholder", f.placeholder);
            f.value = collapseWs(decodeEntities(inner));
            b.doc.fields.push_back(f);
            Pen p = pen();
            p.link = -1;
            b.doc.focus.push_back({1, static_cast<int>(b.doc.fields.size()) - 1, -1});
            p.focus = static_cast<int>(b.doc.focus.size()) - 1;
            b.block(0);
            b.addBox(SK_INPUT, std::max(sc(160), std::min(sc(560), b.W - b.indent)), sc(32), static_cast<int>(b.doc.fields.size()) - 1, p);
            b.block(0);
            continue;
        }
        if (name == "select" && !closing) {
            const size_t z = findCI(html, "</select", i);
            const std::string inner = html.substr(i, z == std::string::npos ? n - i : z - i);
            if (z == std::string::npos) i = n; else { size_t z2 = html.find('>', z); i = z2 == std::string::npos ? n : z2 + 1; }
            std::string nm;
            getAttr(attrs, "name", nm);
            std::string chosen, first;
            bool haveFirst = false, haveSel = false;
            for (size_t o = findCI(inner, "<option", 0); o != std::string::npos; o = findCI(inner, "<option", o + 7)) {
                size_t oe = tagEnd(inner, o + 1);
                if (oe == std::string::npos) break;
                const std::string oa = inner.substr(o + 7, oe - o - 7);
                std::string v, sel;
                size_t nx = inner.find('<', oe + 1);
                const std::string label = collapseWs(decodeEntities(inner.substr(oe + 1, (nx == std::string::npos ? inner.size() : nx) - oe - 1)));
                if (!getAttr(oa, "value", v)) v = label;
                if (!haveFirst) { first = v; haveFirst = true; }
                if (!haveSel && getAttr(oa, "selected", sel)) { chosen = v; haveSel = true; }
            }
            if (!nm.empty()) {
                FormField f;
                f.form = curForm; f.type = "hidden"; f.name = nm; f.value = haveSel ? chosen : first;
                b.doc.fields.push_back(f);
            }
            continue;
        }
        if (name == "button" && !closing) {
            const size_t z = findCI(html, "</button", i);
            const std::string inner = html.substr(i, z == std::string::npos ? n - i : z - i);
            if (z == std::string::npos) i = n; else { size_t z2 = html.find('>', z); i = z2 == std::string::npos ? n : z2 + 1; }
            std::string label = collapseWs(decodeEntities(stripTags(inner)));
            std::string type, aria;
            getAttr(attrs, "type", type);
            if (label.empty()) { if (!getAttr(attrs, "aria-label", label)) getAttr(attrs, "title", label); }
            if (label.empty()) label = "Enviar";
            FormField f;
            f.form = curForm;
            f.type = lowerStr(type) == "button" ? "button" : "submit";
            getAttr(attrs, "name", f.name);
            getAttr(attrs, "value", f.value);
            b.doc.fields.push_back(f);
            Pen p = pen();
            p.link = -1;
            b.doc.focus.push_back({1, static_cast<int>(b.doc.fields.size()) - 1, -1});
            p.focus = static_cast<int>(b.doc.focus.size()) - 1;
            label = te_fit(label, sc(260), FONT_SANS, baseSize, false);
            b.addBox(SK_BUTTON, te_width(label, FONT_SANS, baseSize, false) + sc(28), sc(32), static_cast<int>(b.doc.fields.size()) - 1, p, label);
            continue;
        }
        if (isBlock1(name)) { if (inCell > 0 && (name == "div" || name == "center" || name == "nav" || name == "section" || name == "header" || name == "figure")) continue; b.block(0); continue; }
        // resto (span sin color, em, i, label, sup, sub...): en linea, sin efecto
    }
    b.newline();
    b.doc.height = b.curY + sc(24);
    return std::move(b.doc);
}

Document parsePlain(const std::string& text, int width, int zoomPct) {
    Builder b(width);
    Pen p;
    p.font = FONT_MONO;
    p.size = std::max(9, static_cast<int>(std::lround(14 * zoomPct / 100.0)));
    p.role = ROLE_CODE;
    b.addPre(text, p);
    b.newline();
    b.doc.height = b.curY + 24;
    return std::move(b.doc);
}

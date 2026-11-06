#include "osk.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "canvas.h"
#include "textengine.h"
#include "theme.h"

namespace {

enum KeyKind { KK_CHAR, KK_TEXT, KK_SHIFT, KK_SYM, KK_ABC, KK_ACC, KK_BACK, KK_SPACE, KK_SUBMIT, KK_CANCEL, KK_CLEFT, KK_CRIGHT };
struct Key {
    const char* label;   // lo que se ve (para KK_CHAR tambien es lo que se escribe)
    const char* value;   // texto a insertar para KK_TEXT (nullptr = usar label)
    float w;             // ancho relativo
    KeyKind kind;
};
typedef std::vector<Key> Row;

#define CH(s) Key{s, nullptr, 1.0f, KK_CHAR}
static const Row kDigits = {CH("1"), CH("2"), CH("3"), CH("4"), CH("5"), CH("6"), CH("7"), CH("8"), CH("9"), CH("0"),
                             Key{"Cerrar", nullptr, 1.9f, KK_CANCEL}};
static const Row kBottomAbc = {Key{"123", nullptr, 1.4f, KK_SYM}, Key{"\xC3\xA1\xC3\xA9\xC3\xAD", nullptr, 1.5f, KK_ACC},
                               Key{"<", nullptr, 1.0f, KK_CLEFT}, Key{"Espacio", nullptr, 3.6f, KK_SPACE},
                               Key{">", nullptr, 1.0f, KK_CRIGHT}, CH("."), CH("/"), Key{".com", ".com", 1.5f, KK_TEXT},
                               Key{"Ir", nullptr, 1.6f, KK_SUBMIT}};
static const Row kBottomSym = {Key{"ABC", nullptr, 1.4f, KK_ABC}, Key{"\xC3\xA1\xC3\xA9\xC3\xAD", nullptr, 1.5f, KK_ACC},
                               Key{"<", nullptr, 1.0f, KK_CLEFT}, Key{"Espacio", nullptr, 3.6f, KK_SPACE},
                               Key{">", nullptr, 1.0f, KK_CRIGHT}, CH("."), CH("/"), Key{".com", ".com", 1.5f, KK_TEXT},
                               Key{"Ir", nullptr, 1.6f, KK_SUBMIT}};
static const Row kBottomAcc = {Key{"ABC", nullptr, 1.4f, KK_ABC}, Key{"123", nullptr, 1.5f, KK_SYM},
                               Key{"<", nullptr, 1.0f, KK_CLEFT}, Key{"Espacio", nullptr, 3.6f, KK_SPACE},
                               Key{">", nullptr, 1.0f, KK_CRIGHT}, CH("."), CH("/"), Key{".com", ".com", 1.5f, KK_TEXT},
                               Key{"Ir", nullptr, 1.6f, KK_SUBMIT}};

static const std::vector<Row>& layerRows(int layer) {
    static const std::vector<Row> abc = {
        kDigits,
        {CH("q"), CH("w"), CH("e"), CH("r"), CH("t"), CH("y"), CH("u"), CH("i"), CH("o"), CH("p")},
        {CH("a"), CH("s"), CH("d"), CH("f"), CH("g"), CH("h"), CH("j"), CH("k"), CH("l"), CH("\xC3\xB1")},
        {Key{"May\xC3\xBAs", nullptr, 1.7f, KK_SHIFT}, CH("z"), CH("x"), CH("c"), CH("v"), CH("b"), CH("n"), CH("m"), Key{"Borrar", nullptr, 1.7f, KK_BACK}},
        kBottomAbc};
    static const std::vector<Row> sym = {
        kDigits,
        {CH("-"), CH("_"), CH("="), CH("+"), CH("&"), CH("/"), CH("\\"), CH("|"), CH("~"), CH("`")},
        {CH(":"), CH(";"), CH("'"), CH("\""), CH("?"), CH("!"), CH(","), CH("."), CH("@"), CH("#")},
        {CH("("), CH(")"), CH("["), CH("]"), CH("{"), CH("}"), CH("<"), CH(">"), CH("$"), CH("%"), Key{"Borrar", nullptr, 1.7f, KK_BACK}},
        kBottomSym};
    static const std::vector<Row> acc = {
        kDigits,
        {CH("\xC3\xA1"), CH("\xC3\xA9"), CH("\xC3\xAD"), CH("\xC3\xB3"), CH("\xC3\xBA"), CH("\xC3\xBC"), CH("\xC3\xB1"), CH("\xC2\xBF"), CH("\xC2\xA1"), CH("\xC3\xA7")},
        {CH("\xC3\xA0"), CH("\xC3\xA8"), CH("\xC3\xAC"), CH("\xC3\xB2"), CH("\xC3\xB9"), CH("\xC3\xA2"), CH("\xC3\xAA"), CH("\xC3\xAE"), CH("\xC3\xB4"), CH("\xC3\xBB")},
        {Key{"May\xC3\xBAs", nullptr, 1.7f, KK_SHIFT}, CH("\xC3\xA4"), CH("\xC3\xAB"), CH("\xC3\xAF"), CH("\xC3\xB6"), CH("\xC3\xA3"), CH("\xC3\xB5"), CH("\xC3\xA5"), CH("\xC3\xB8"), Key{"Borrar", nullptr, 1.7f, KK_BACK}},
        kBottomAcc};
    return layer == 1 ? sym : (layer == 2 ? acc : abc);
}

// Codepoint UTF-8 de un caracter (o 0 si no es un solo caracter).
uint32_t singleCp(const char* s) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    if (!p[0]) return 0;
    if (p[0] < 0x80) return p[1] ? 0 : p[0];
    if ((p[0] & 0xE0) == 0xC0 && p[1] && !p[2]) return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    return 0;
}
void appendCp(std::string& o, uint32_t cp) {
    if (cp < 0x80) o += static_cast<char>(cp);
    else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
}
// Mayuscula de las letras latinas habituales (ASCII y Latin-1).
uint32_t upperCp(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 32;
    return cp;
}
std::string keyText(const Key& k, int shift) {  // lo que muestra / escribe una tecla de caracter
    if (k.kind != KK_CHAR) return k.value ? k.value : k.label;
    const uint32_t cp = singleCp(k.label);
    if (shift > 0 && cp) { std::string o; appendCp(o, upperCp(cp)); return o; }
    return k.label;
}
const int kMaxBytes = 2000;

// Geometria del panel.
struct Geo {
    int px, py, pw, ph;       // panel
    int fx, fy, fw, fh;       // campo de texto
    int ky0, kh, kgap;        // primera fila de teclas, alto y separacion
};
Geo geoFor(int W, int H) {
    Geo g;
    g.pw = std::min(W - 60, 1320);
    g.kh = 62; g.kgap = 8;
    g.fh = 62;
    g.ph = 20 + 28 + g.fh + 16 + 5 * (g.kh + g.kgap) + 40;
    g.px = (W - g.pw) / 2;
    g.py = H - g.ph - 18;
    g.fx = g.px + 24; g.fy = g.py + 20 + 28; g.fw = g.pw - 48;
    g.ky0 = g.fy + g.fh + 16;
    return g;
}
struct Rect { int x, y, w, h; };
// Rectangulos de las teclas de una fila (llenan el ancho disponible).
std::vector<Rect> rowRects(const Row& r, const Geo& g, int rowIdx) {
    float total = 0;
    for (const Key& k : r) total += k.w;
    const int availW = g.pw - 48 - static_cast<int>(r.size() - 1) * g.kgap;
    std::vector<Rect> out;
    float acc = 0;
    const int y = g.ky0 + rowIdx * (g.kh + g.kgap);
    for (size_t i = 0; i < r.size(); i++) {
        const int x0 = g.px + 24 + static_cast<int>(acc / total * availW) + static_cast<int>(i) * g.kgap;
        acc += r[i].w;
        const int x1 = g.px + 24 + static_cast<int>(acc / total * availW) + static_cast<int>(i) * g.kgap;
        out.push_back(Rect{x0, y, x1 - x0, g.kh});
    }
    return out;
}
// Tecla de la fila 'to' mas cercana (por el centro) a la tecla (row,col).
int nearestCol(int fromRow, int fromCol, int toRow, int layer) {
    static const Geo g = geoFor(1920, 1080);
    const std::vector<Row>& rows = layerRows(layer);
    const std::vector<Rect> a = rowRects(rows[fromRow], g, fromRow);
    const std::vector<Rect> b = rowRects(rows[toRow], g, toRow);
    const int cx = a[std::min<size_t>(fromCol, a.size() - 1)].x + a[std::min<size_t>(fromCol, a.size() - 1)].w / 2;
    int best = 0, bestD = 1 << 30;
    for (size_t i = 0; i < b.size(); i++) {
        const int c = b[i].x + b[i].w / 2;
        const int d = std::abs(c - cx);
        if (d < bestD) { bestD = d; best = static_cast<int>(i); }
    }
    return best;
}
void clampSel(Osk& k) {
    const std::vector<Row>& rows = layerRows(k.layer);
    k.row = std::max(0, std::min(static_cast<int>(rows.size()) - 1, k.row));
    k.col = std::max(0, std::min(static_cast<int>(rows[k.row].size()) - 1, k.col));
}
bool insertText(Osk& k, const std::string& s) {
    if (k.text.size() + s.size() > static_cast<size_t>(kMaxBytes)) return false;
    k.text.insert(k.cur, s);
    k.cur += s.size();
    return true;
}
bool eraseBefore(Osk& k) {
    if (k.cur == 0) return false;
    const size_t p = utf8_prev_pos(k.text, k.cur);
    k.text.erase(p, k.cur - p);
    k.cur = p;
    return true;
}

}  // namespace

size_t utf8_prev_pos(const std::string& s, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) pos--;
    return pos;
}
size_t utf8_next_pos(const std::string& s, size_t pos) {
    if (pos >= s.size()) return s.size();
    pos++;
    while (pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) pos++;
    return pos;
}
void utf8_pop_back(std::string& s) {
    if (s.empty()) return;
    s.erase(utf8_prev_pos(s, s.size()));
}

void osk_open(Osk& k, const std::string& title, const std::string& initial, bool password) {
    k.open = true;
    k.password = password;
    k.title = title;
    k.text = initial;
    k.cur = k.text.size();
    k.layer = 0;
    k.shift = 0;
    k.row = 1;
    k.col = 0;
}

OskResult osk_input(Osk& k, OskInput in) {
    if (!k.open) return OSK_NONE;
    const std::vector<Row>& rows = layerRows(k.layer);
    const int nr = static_cast<int>(rows.size());
    switch (in) {
        case OSK_LEFT: k.col = (k.col + static_cast<int>(rows[k.row].size()) - 1) % static_cast<int>(rows[k.row].size()); return OSK_REDRAW;
        case OSK_RIGHT: k.col = (k.col + 1) % static_cast<int>(rows[k.row].size()); return OSK_REDRAW;
        case OSK_UP: case OSK_DOWN: {
            const int nrow = (k.row + (in == OSK_UP ? nr - 1 : 1)) % nr;
            k.col = nearestCol(k.row, k.col, nrow, k.layer);
            k.row = nrow;
            return OSK_REDRAW;
        }
        case OSK_BACKSPACE:
            if (k.text.empty()) return OSK_CANCELLED;
            return eraseBefore(k) ? OSK_CHANGED : OSK_NONE;
        case OSK_SPACE: return insertText(k, " ") ? OSK_CHANGED : OSK_NONE;
        case OSK_SHIFT: k.shift = (k.shift + 1) % 3; return OSK_REDRAW;
        case OSK_CURSOR_L: { const size_t p = utf8_prev_pos(k.text, k.cur); if (p == k.cur) return OSK_NONE; k.cur = p; return OSK_REDRAW; }
        case OSK_CURSOR_R: { const size_t p = utf8_next_pos(k.text, k.cur); if (p == k.cur) return OSK_NONE; k.cur = p; return OSK_REDRAW; }
        case OSK_SELECT: break;
    }
    clampSel(k);
    const Key& key = rows[k.row][k.col];
    switch (key.kind) {
        case KK_CHAR: {
            const bool ok = insertText(k, keyText(key, k.layer == 1 ? 0 : k.shift));
            if (ok && k.shift == 1) k.shift = 0;  // Mayus de un toque: se apaga tras una letra
            return ok ? OSK_CHANGED : OSK_NONE;
        }
        case KK_TEXT: return insertText(k, key.value ? key.value : key.label) ? OSK_CHANGED : OSK_NONE;
        case KK_SPACE: return insertText(k, " ") ? OSK_CHANGED : OSK_NONE;
        case KK_BACK: return eraseBefore(k) ? OSK_CHANGED : OSK_NONE;
        case KK_SHIFT: k.shift = (k.shift + 1) % 3; return OSK_REDRAW;
        case KK_SYM: k.layer = 1; clampSel(k); return OSK_REDRAW;
        case KK_ABC: k.layer = 0; clampSel(k); return OSK_REDRAW;
        case KK_ACC: k.layer = 2; clampSel(k); return OSK_REDRAW;
        case KK_SUBMIT: return OSK_SUBMITTED;
        case KK_CANCEL: return OSK_CANCELLED;
        case KK_CLEFT: return osk_input(k, OSK_CURSOR_L);
        case KK_CRIGHT: return osk_input(k, OSK_CURSOR_R);
    }
    return OSK_NONE;
}

static int baseFor(int y, int h, int font, int px) { return y + h / 2 + (te_ascent(font, px) - te_descent(font, px)) / 2; }

void osk_draw(const Osk& k, Canvas& cv, const Theme& th, int W, int H, bool caretOn) {
    if (!k.open) return;
    const Geo g = geoFor(W, H);
    const uint32_t keyBg = th.inputBg, keyBorder = th.btnBorder;
    const uint32_t selBg = th.accent, selText = th.dark ? th.bg : 0xFFFFFFFFu;
    cv.resetClip();
    cv.rrect(g.px + 4, g.py + 6, g.pw, g.ph, 18, 0x55000000);  // sombra
    cv.rrectBorder(g.px, g.py, g.pw, g.ph, 18, 2, th.omniBorder, th.tabbar);
    te_draw(cv, g.px + 28, g.py + 36, k.title.empty() ? std::string("Escribir") : k.title, FONT_SANS, 17, false, th.dim);
    // campo de texto con cursor
    cv.rrectBorder(g.fx, g.fy, g.fw, g.fh, 10, 2, th.accent, th.inputBg);
    std::string shown = k.text;
    size_t curShown = k.cur;
    if (k.password) {
        std::string b;
        size_t nb = 0;
        for (size_t i = 0; i < k.text.size(); i = utf8_next_pos(k.text, i)) { b += "\xE2\x80\xA2"; if (i < k.cur) nb = b.size(); }
        if (k.cur >= k.text.size()) nb = b.size();
        shown = b; curShown = nb;
    }
    const int tpx = 30;
    const int tw = te_width(shown.substr(0, curShown), FONT_SANS, tpx, false);
    const int fullW = te_width(shown, FONT_SANS, tpx, false);
    const int maxw = g.fw - 28;
    int off = 0;
    if (tw > maxw - 4) off = tw - (maxw - 4);
    (void)fullW;
    cv.setClip(g.fx + 12, g.fy, g.fx + g.fw - 12, g.fy + g.fh);
    if (shown.empty()) te_draw(cv, g.fx + 16, baseFor(g.fy, g.fh, FONT_SANS, tpx), "...", FONT_SANS, tpx, false, th.dim);
    else te_draw(cv, g.fx + 16 - off, baseFor(g.fy, g.fh, FONT_SANS, tpx), shown, FONT_SANS, tpx, false, th.text);
    if (caretOn) cv.rect(g.fx + 16 - off + tw + 1, g.fy + 10, 2, g.fh - 20, th.text);
    cv.resetClip();
    // teclas
    const std::vector<Row>& rows = layerRows(k.layer);
    for (size_t r = 0; r < rows.size(); r++) {
        const std::vector<Rect> rc = rowRects(rows[r], g, static_cast<int>(r));
        for (size_t c = 0; c < rows[r].size(); c++) {
            const Key& key = rows[r][c];
            const bool sel = static_cast<int>(r) == k.row && static_cast<int>(c) == k.col;
            const Rect& a = rc[c];
            const bool lit = (key.kind == KK_SHIFT && k.shift > 0);
            cv.rrectBorder(a.x, a.y, a.w, a.h, 10, sel ? 3 : 1, sel ? th.accent : keyBorder, sel ? selBg : (lit ? th.focusBg : keyBg));
            std::string label;
            if (key.kind == KK_SHIFT) label = k.shift == 2 ? "BLOQ" : (k.shift == 1 ? "MAY\xC3\x9AS" : key.label);
            else label = keyText(key, (k.layer == 1) ? 0 : k.shift);
            const bool big = key.kind == KK_CHAR || key.kind == KK_CLEFT || key.kind == KK_CRIGHT;
            const int px = big ? 28 : 22;
            const int w = te_width(label, FONT_SANS, px, false);
            te_draw(cv, a.x + (a.w - w) / 2, baseFor(a.y, a.h, FONT_SANS, px), label, FONT_SANS, px, false, sel ? selText : th.text);
        }
    }
    const int hy = g.ky0 + 5 * (g.kh + g.kgap) + 12;
    te_draw(cv, g.px + 28, hy + 14, "X escribir   O borrar (vacio: cerrar)   Cuadrado espacio   Triangulo mayusculas", FONT_SANS, 16, false, th.dim);
}

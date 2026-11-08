// Arbol DOM + selectores + CSS lite (ver dom.h).
#include "dom.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>

// ------------------------------------------------------------------ utilidades de texto
static std::string lower(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}
static bool isWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isWs(s[a])) a++;
    while (b > a && isWs(s[b - 1])) b--;
    return s.substr(a, b - a);
}
static void utf8Append(std::string& o, unsigned cp) {
    if (cp < 0x80) o += static_cast<char>(cp);
    else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
}
static const struct { const char* n; unsigned cp; } kEnt[] = {
    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0}, {"copy", 0xA9}, {"reg", 0xAE},
    {"hellip", 0x2026}, {"mdash", 0x2014}, {"ndash", 0x2013}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C},
    {"rdquo", 0x201D}, {"bull", 0x2022}, {"middot", 0xB7}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"euro", 0x20AC},
    {"trade", 0x2122}, {"times", 0xD7}, {"deg", 0xB0}, {"plusmn", 0xB1}, {"iexcl", 0xA1}, {"iquest", 0xBF},
    {"aacute", 0xE1}, {"eacute", 0xE9}, {"iacute", 0xED}, {"oacute", 0xF3}, {"uacute", 0xFA}, {"ntilde", 0xF1},
    {"Aacute", 0xC1}, {"Eacute", 0xC9}, {"Iacute", 0xCD}, {"Oacute", 0xD3}, {"Uacute", 0xDA}, {"Ntilde", 0xD1},
    {"uuml", 0xFC}, {"Uuml", 0xDC}, {"auml", 0xE4}, {"ouml", 0xF6}, {"agrave", 0xE0}, {"egrave", 0xE8}, {"ccedil", 0xE7},
    {"larr", 0x2190}, {"rarr", 0x2192}, {"uarr", 0x2191}, {"darr", 0x2193}, {"hearts", 0x2665}, {"cent", 0xA2},
    {"pound", 0xA3}, {"yen", 0xA5}, {"sect", 0xA7}, {"para", 0xB6}, {"frac12", 0xBD}, {"ensp", 0x2002}, {"emsp", 0x2003},
    {"thinsp", 0x2009}, {nullptr, 0}};

std::string domDecodeEntities(const std::string& s) {
    if (s.find('&') == std::string::npos) return s;
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '&') { o += s[i]; continue; }
        size_t j = i + 1;
        if (j < s.size() && s[j] == '#') {
            j++;
            bool hex = j < s.size() && (s[j] == 'x' || s[j] == 'X');
            if (hex) j++;
            size_t k = j;
            unsigned cp = 0;
            while (k < s.size() && k - j < 8 && (hex ? std::isxdigit(static_cast<unsigned char>(s[k])) : std::isdigit(static_cast<unsigned char>(s[k]))))
                cp = cp * (hex ? 16 : 10) + static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(s[k])) ? s[k] - '0' : (std::tolower(s[k]) - 'a' + 10)), k++;
            if (k > j && cp > 0 && cp < 0x110000) {
                utf8Append(o, cp);
                i = (k < s.size() && s[k] == ';') ? k : k - 1;
                continue;
            }
        } else {
            size_t k = j;
            while (k < s.size() && k - j < 10 && std::isalnum(static_cast<unsigned char>(s[k]))) k++;
            if (k < s.size() && s[k] == ';' && k > j) {
                const std::string nm = s.substr(j, k - j);
                bool found = false;
                for (int e = 0; kEnt[e].n; e++)
                    if (nm == kEnt[e].n) { utf8Append(o, kEnt[e].cp); found = true; break; }
                if (found) { i = k; continue; }
            }
        }
        o += '&';
    }
    return o;
}
// Escapa '&' solo si no inicia ya una entidad (&nombre; o &#n;) para no romper entidades que no sabemos decodificar.
static bool looksLikeEntity(const std::string& s, size_t i) {
    size_t j = i + 1;
    if (j < s.size() && s[j] == '#') j++;
    size_t k = j;
    while (k < s.size() && k - j < 10 && std::isalnum(static_cast<unsigned char>(s[k]))) k++;
    return k > j && k < s.size() && s[k] == ';';
}
std::string domEscapeText(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); i++) {
        const char c = s[i];
        if (c == '&') o += looksLikeEntity(s, i) ? "&" : "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else o += c;
    }
    return o;
}
std::string domEscapeAttr(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); i++) {
        const char c = s[i];
        if (c == '&') o += looksLikeEntity(s, i) ? "&" : "&amp;";
        else if (c == '"') o += "&quot;";
        else if (c == '<') o += "&lt;";
        else o += c;
    }
    return o;
}

// ------------------------------------------------------------------ selectores
namespace {
struct Complex;
struct AttrCond { std::string name, op, val; };
struct Simple {
    std::string tag;  // "" = cualquiera
    std::string id;
    std::vector<std::string> classes;
    std::vector<AttrCond> attrs;
    struct Nth { bool on = false; bool last = false; int a = 0, b = 0; };
    std::vector<Nth> nth;      // :first-child (0n+1), :last-child, :nth-child
    bool onlyChild = false, empty = false, root = false, checked = false, never = false;
    std::vector<Complex> nots;
};
struct Complex {
    std::vector<Simple> parts;
    std::vector<char> combs;  // combs[i] entre parts[i] y parts[i+1]
};

bool isIdent(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || (static_cast<unsigned char>(c) >= 0x80); }

struct SelParser {
    const std::string& s;
    size_t i = 0;
    explicit SelParser(const std::string& str) : s(str) {}
    bool eof() const { return i >= s.size(); }
    void skipWs() { while (i < s.size() && isWs(s[i])) i++; }
    std::string ident() {
        std::string r;
        while (i < s.size()) {
            if (s[i] == '\\' && i + 1 < s.size()) { r += s[i + 1]; i += 2; continue; }
            if (!isIdent(s[i])) break;
            r += s[i++];
        }
        return r;
    }
    static void parseNth(const std::string& raw, Simple::Nth& n) {
        std::string t = lower(trim(raw));
        n.on = true;
        if (t == "odd") { n.a = 2; n.b = 1; return; }
        if (t == "even") { n.a = 2; n.b = 0; return; }
        size_t p = t.find('n');
        if (p == std::string::npos) { n.a = 0; n.b = std::atoi(t.c_str()); return; }
        std::string as = trim(t.substr(0, p)), bs = trim(t.substr(p + 1));
        n.a = (as.empty() || as == "+") ? 1 : (as == "-" ? -1 : std::atoi(as.c_str()));
        std::string b2;
        for (char c : bs) if (c != ' ') b2 += c;
        n.b = b2.empty() ? 0 : std::atoi(b2.c_str());
    }
    bool simple(Simple& out) {
        bool any = false;
        while (i < s.size()) {
            const char c = s[i];
            if (c == '*') { i++; any = true; }
            else if (c == '#') { i++; out.id = ident(); any = true; }
            else if (c == '.') { i++; out.classes.push_back(ident()); any = true; }
            else if (c == '[') {
                i++;
                skipWs();
                AttrCond ac;
                ac.name = lower(ident());
                skipWs();
                if (i < s.size() && s[i] != ']') {
                    if (s[i] == '=') { ac.op = "="; i++; }
                    else if (i + 1 < s.size() && s[i + 1] == '=' && std::strchr("~|^$*", s[i]) != nullptr) { ac.op = std::string(1, s[i]) + "="; i += 2; }
                    else return false;
                    skipWs();
                    if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
                        const char q = s[i++];
                        while (i < s.size() && s[i] != q) ac.val += s[i++];
                        if (i < s.size()) i++;
                    } else {
                        while (i < s.size() && s[i] != ']' && !isWs(s[i])) ac.val += s[i++];
                    }
                    skipWs();
                    if (i < s.size() && (s[i] == 'i' || s[i] == 'I')) { i++; skipWs(); }
                }
                if (i >= s.size() || s[i] != ']') return false;
                i++;
                out.attrs.push_back(ac);
                any = true;
            } else if (c == ':') {
                i++;
                if (i < s.size() && s[i] == ':') { i++; ident(); out.never = true; any = true; continue; }  // ::before etc.
                std::string nm = lower(ident());
                std::string arg;
                if (i < s.size() && s[i] == '(') {
                    int depth = 1;
                    i++;
                    while (i < s.size() && depth > 0) {
                        if (s[i] == '(') depth++;
                        else if (s[i] == ')') { depth--; if (depth == 0) break; }
                        arg += s[i++];
                    }
                    if (i < s.size()) i++;
                }
                if (nm == "first-child") { Simple::Nth n; n.on = true; n.a = 0; n.b = 1; out.nth.push_back(n); }
                else if (nm == "last-child") { Simple::Nth n; n.on = true; n.last = true; n.a = 0; n.b = 1; out.nth.push_back(n); }
                else if (nm == "nth-child") { Simple::Nth n; parseNth(arg, n); out.nth.push_back(n); }
                else if (nm == "nth-last-child") { Simple::Nth n; parseNth(arg, n); n.last = true; out.nth.push_back(n); }
                else if (nm == "only-child") out.onlyChild = true;
                else if (nm == "empty") out.empty = true;
                else if (nm == "root") out.root = true;
                else if (nm == "checked") out.checked = true;
                else if (nm == "not") {
                    Complex cx;
                    SelParser sub(arg);
                    if (!sub.complex(cx)) return false;
                    out.nots.push_back(cx);
                } else out.never = true;  // :hover, :focus, :active, :visited, :before...
                any = true;
            } else if (isIdent(c) || c == '\\') {
                if (any) break;
                out.tag = lower(ident());
                any = true;
            } else break;
        }
        return any;
    }
    bool complex(Complex& cx) {
        skipWs();
        Simple sp;
        if (!simple(sp)) return false;
        cx.parts.push_back(sp);
        while (true) {
            const size_t save = i;
            skipWs();
            if (eof() || s[i] == ',') break;
            char comb = ' ';
            if (s[i] == '>' || s[i] == '+' || s[i] == '~') { comb = s[i++]; skipWs(); }
            else if (i == save) return false;
            Simple nx;
            if (!simple(nx)) return false;
            cx.combs.push_back(comb);
            cx.parts.push_back(nx);
        }
        return true;
    }
    bool list(std::vector<Complex>& out) {
        while (true) {
            Complex cx;
            if (!complex(cx)) return false;
            out.push_back(std::move(cx));
            skipWs();
            if (eof()) return true;
            if (s[i] != ',') return false;
            i++;
        }
    }
};

bool hasClass(const DomNode* n, const std::string& cls) {
    const std::string* c = Dom::getAttr(n, "class");
    if (!c) return false;
    const std::string& v = *c;
    size_t p = 0;
    while (p < v.size()) {
        while (p < v.size() && isWs(v[p])) p++;
        size_t q = p;
        while (q < v.size() && !isWs(v[q])) q++;
        if (q > p && v.compare(p, q - p, cls) == 0 && q - p == cls.size()) return true;
        p = q;
    }
    return false;
}
int elemIndex(const DomNode* n, bool fromEnd, int* total) {
    int idx = 0, cnt = 0, my = 0;
    if (!n->parent) { if (total) *total = 1; return 1; }
    for (const DomNode* k : n->parent->kids) {
        if (k->type != DomNode::ELEMENT) continue;
        cnt++;
        if (k == n) my = cnt;
    }
    idx = fromEnd ? cnt - my + 1 : my;
    if (total) *total = cnt;
    return idx;
}
bool nthMatch(int a, int b, int idx) {
    if (a == 0) return idx == b;
    const int d = idx - b;
    return d % a == 0 && d / a >= 0;
}
bool matchComplex(const DomNode* n, const Complex& c, int i);
bool matchSimple(const DomNode* n, const Simple& s) {
    if (n->type != DomNode::ELEMENT || s.never) return false;
    if (!s.tag.empty() && s.tag != n->tag) return false;
    if (!s.id.empty()) {
        const std::string* v = Dom::getAttr(n, "id");
        if (!v || *v != s.id) return false;
    }
    for (const auto& c : s.classes) if (!hasClass(n, c)) return false;
    for (const auto& ac : s.attrs) {
        const std::string* v = Dom::getAttr(n, ac.name);
        if (!v) return false;
        if (ac.op.empty()) continue;
        const std::string& x = *v;
        const std::string& t = ac.val;
        if (ac.op == "=") { if (x != t) return false; }
        else if (ac.op == "^=") { if (t.empty() || x.compare(0, t.size(), t) != 0) return false; }
        else if (ac.op == "$=") { if (t.empty() || x.size() < t.size() || x.compare(x.size() - t.size(), t.size(), t) != 0) return false; }
        else if (ac.op == "*=") { if (t.empty() || x.find(t) == std::string::npos) return false; }
        else if (ac.op == "~=") { if (!hasClass(n, t) && (x.find(t) == std::string::npos)) return false; }
        else if (ac.op == "|=") { if (x != t && x.compare(0, t.size() + 1, t + "-") != 0) return false; }
    }
    for (const auto& nt : s.nth) {
        int tot = 0;
        const int idx = elemIndex(n, nt.last, &tot);
        if (!nthMatch(nt.a, nt.b, idx)) return false;
    }
    if (s.onlyChild) { int tot = 0; elemIndex(n, false, &tot); if (tot != 1) return false; }
    if (s.empty && !n->kids.empty()) return false;
    if (s.root && !(n->parent && n->parent->type == DomNode::DOCUMENT)) return false;
    if (s.checked && !Dom::getAttr(n, "checked")) return false;
    for (const auto& nc : s.nots) if (matchComplex(n, nc, static_cast<int>(nc.parts.size()) - 1)) return false;
    return true;
}
const DomNode* prevElem(const DomNode* n) {
    if (!n->parent) return nullptr;
    const DomNode* prev = nullptr;
    for (const DomNode* k : n->parent->kids) {
        if (k == n) return prev;
        if (k->type == DomNode::ELEMENT) prev = k;
    }
    return nullptr;
}
bool matchComplex(const DomNode* n, const Complex& c, int i) {
    if (!matchSimple(n, c.parts[i])) return false;
    if (i == 0) return true;
    const char comb = c.combs[i - 1];
    if (comb == '>') { const DomNode* p = n->parent; return p && p->type == DomNode::ELEMENT && matchComplex(p, c, i - 1); }
    if (comb == ' ') {
        for (const DomNode* p = n->parent; p; p = p->parent)
            if (p->type == DomNode::ELEMENT && matchComplex(p, c, i - 1)) return true;
        return false;
    }
    if (comb == '+') { const DomNode* p = prevElem(n); return p && matchComplex(p, c, i - 1); }
    // '~'
    for (const DomNode* p = prevElem(n); p; p = prevElem(p))
        if (matchComplex(p, c, i - 1)) return true;
    return false;
}
int specificity(const Complex& c) {
    int ids = 0, cls = 0, tags = 0;
    std::vector<const Complex*> stack{&c};
    while (!stack.empty()) {
        const Complex* x = stack.back();
        stack.pop_back();
        for (const auto& p : x->parts) {
            if (!p.id.empty()) ids++;
            cls += static_cast<int>(p.classes.size() + p.attrs.size() + p.nth.size()) + (p.onlyChild ? 1 : 0) + (p.checked ? 1 : 0);
            if (!p.tag.empty()) tags++;
            for (const auto& n : p.nots) stack.push_back(&n);
        }
    }
    return std::min(ids, 99) * 10000 + std::min(cls, 99) * 100 + std::min(tags, 99);
}
}  // namespace

// ------------------------------------------------------------------ reglas CSS lite
struct Dom::Rules {
    struct Rule {
        Complex sel;
        int spec = 0, order = 0;
        std::string display;   // "" = no define
        bool displayImp = false;
        int vis = -1;          // -1 no define, 0 visible, 1 hidden
        bool visImp = false;
        std::string color;     // "" = no define
        bool colorImp = false;
    };
    std::vector<Rule> all;
    std::map<std::string, std::vector<int>> byId, byClass, byTag;
    std::vector<int> other;
};

static void parseDecls(const std::string& body, Dom::Rules::Rule& r) {
    size_t p = 0;
    while (p < body.size()) {
        size_t e = body.find(';', p);
        if (e == std::string::npos) e = body.size();
        const std::string d = body.substr(p, e - p);
        p = e + 1;
        const size_t c = d.find(':');
        if (c == std::string::npos) continue;
        const std::string prop = lower(trim(d.substr(0, c)));
        std::string val = trim(d.substr(c + 1));
        bool imp = false;
        const size_t ip = lower(val).find("!important");
        if (ip != std::string::npos) { imp = true; val = trim(val.substr(0, ip)); }
        val = lower(val);
        if (prop == "display") { r.display = val; r.displayImp = imp; }
        else if (prop == "visibility") { r.vis = (val == "hidden" || val == "collapse") ? 1 : 0; r.visImp = imp; }
        else if (prop == "color") { r.color = val; r.colorImp = imp; }
    }
}

static void parseCss(const std::string& cssIn, Dom::Rules& rules, int& order) {
    std::string css;
    css.reserve(cssIn.size());
    for (size_t i = 0; i < cssIn.size(); i++) {  // quitar comentarios
        if (cssIn[i] == '/' && i + 1 < cssIn.size() && cssIn[i + 1] == '*') {
            const size_t e = cssIn.find("*/", i + 2);
            if (e == std::string::npos) break;
            i = e + 1;
        } else css += cssIn[i];
    }
    size_t i = 0;
    while (i < css.size()) {
        while (i < css.size() && isWs(css[i])) i++;
        if (i >= css.size()) break;
        if (css[i] == '@') {  // at-rule: se salta (los @media quedan fuera a proposito)
            size_t j = i;
            while (j < css.size() && css[j] != '{' && css[j] != ';') j++;
            if (j < css.size() && css[j] == '{') {
                int depth = 1;
                j++;
                while (j < css.size() && depth > 0) { if (css[j] == '{') depth++; else if (css[j] == '}') depth--; j++; }
            } else j++;
            i = j;
            continue;
        }
        const size_t ob = css.find('{', i);
        if (ob == std::string::npos) break;
        const size_t cb = css.find('}', ob);
        if (cb == std::string::npos) break;
        const std::string selText = css.substr(i, ob - i);
        const std::string body = css.substr(ob + 1, cb - ob - 1);
        i = cb + 1;
        Dom::Rules::Rule proto;
        parseDecls(body, proto);
        if (proto.display.empty() && proto.vis < 0 && proto.color.empty()) continue;  // nada que nos interese
        std::vector<Complex> sels;
        SelParser sp(selText);
        if (!sp.list(sels)) {  // lista invalida: probar cada selector por separado
            sels.clear();
            size_t p = 0;
            while (p <= selText.size()) {
                size_t e = selText.find(',', p);
                if (e == std::string::npos) e = selText.size();
                std::string one = trim(selText.substr(p, e - p));
                p = e + 1;
                if (one.empty()) continue;
                Complex cx;
                SelParser s1(one);
                if (s1.complex(cx) && s1.eof()) sels.push_back(std::move(cx));
            }
        }
        for (auto& cx : sels) {
            bool never = false;
            for (const auto& pt : cx.parts) if (pt.never) never = true;
            if (never) continue;  // :hover, ::before...: no se aplican
            Dom::Rules::Rule r = proto;
            r.spec = specificity(cx);
            r.order = order++;
            r.sel = std::move(cx);
            const int idx = static_cast<int>(rules.all.size());
            const Simple& last = r.sel.parts.back();
            if (!last.id.empty()) rules.byId[last.id].push_back(idx);
            else if (!last.classes.empty()) rules.byClass[last.classes[0]].push_back(idx);
            else if (!last.tag.empty()) rules.byTag[last.tag].push_back(idx);
            else rules.other.push_back(idx);
            rules.all.push_back(std::move(r));
        }
        if (rules.all.size() > 20000) break;
    }
}

// ------------------------------------------------------------------ Dom
Dom::Dom() {
    newNode(DomNode::DOCUMENT);
    rules_ = std::make_shared<Rules>();
}
Dom::~Dom() {}

DomNode* Dom::newNode(DomNode::Type t) {
    nodes_.emplace_back(new DomNode());
    DomNode* n = nodes_.back().get();
    n->type = t;
    n->id = static_cast<int>(nodes_.size()) - 1;
    return n;
}
DomNode* Dom::createElement(const std::string& tag) { DomNode* n = newNode(DomNode::ELEMENT); n->tag = lower(tag); return n; }
DomNode* Dom::createText(const std::string& s) { DomNode* n = newNode(DomNode::TEXT); n->text = s; return n; }
DomNode* Dom::createFragment() { return newNode(DomNode::FRAGMENT); }

const std::string* Dom::getAttr(const DomNode* n, const std::string& name) {
    for (const auto& a : n->attrs) if (a.first == name) return &a.second;
    return nullptr;
}

static bool isVoidTag(const std::string& t) {
    static const char* v[] = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr", "command", "keygen", nullptr};
    for (int i = 0; v[i]; i++) if (t == v[i]) return true;
    return false;
}
static bool isRawTag(const std::string& t) { return t == "script" || t == "style" || t == "textarea" || t == "title" || t == "xmp" || t == "plaintext"; }

DomNode* Dom::html() {
    for (DomNode* k : root()->kids) if (k->type == DomNode::ELEMENT && k->tag == "html") return k;
    return nullptr;
}
DomNode* Dom::head() {
    DomNode* h = html();
    if (!h) return nullptr;
    for (DomNode* k : h->kids) if (k->type == DomNode::ELEMENT && k->tag == "head") return k;
    return nullptr;
}
DomNode* Dom::body() {
    DomNode* h = html();
    if (!h) return nullptr;
    for (DomNode* k : h->kids) if (k->type == DomNode::ELEMENT && (k->tag == "body" || k->tag == "frameset")) return k;
    return nullptr;
}

void Dom::parseInto(DomNode* container, const std::string& h) {
    std::vector<DomNode*> st{container};
    const size_t n = h.size();
    size_t i = 0;
    auto addText = [&](const std::string& t, bool raw) {
        if (t.empty()) return;
        DomNode* par = st.back();
        if (!par->kids.empty() && par->kids.back()->type == DomNode::TEXT) { par->kids.back()->text += raw ? t : domDecodeEntities(t); return; }
        DomNode* tn = createText(raw ? t : domDecodeEntities(t));
        tn->parent = par;
        par->kids.push_back(tn);
    };
    auto closeTo = [&](size_t newSize) { while (st.size() > newSize) st.pop_back(); };
    auto findOpen = [&](const char* tag, const char* stop1, const char* stop2, const char* stop3) -> int {
        for (int k = static_cast<int>(st.size()) - 1; k >= 1; k--) {
            const std::string& t = st[k]->tag;
            if (t == tag) return k;
            if ((stop1 && t == stop1) || (stop2 && t == stop2) || (stop3 && t == stop3)) return -1;
        }
        return -1;
    };
    while (i < n) {
        if (h[i] != '<') {
            const size_t e = h.find('<', i);
            size_t end = e == std::string::npos ? n : e;
            addText(h.substr(i, end - i), false);
            i = end;
            continue;
        }
        if (h.compare(i, 4, "<!--") == 0) {
            const size_t e = h.find("-->", i + 4);
            i = e == std::string::npos ? n : e + 3;
            continue;
        }
        if (i + 1 < n && (h[i + 1] == '!' || h[i + 1] == '?')) {
            const size_t e = h.find('>', i);
            i = e == std::string::npos ? n : e + 1;
            continue;
        }
        if (i + 1 < n && h[i + 1] == '/') {  // etiqueta de cierre
            size_t j = i + 2;
            std::string nm;
            while (j < n && !isWs(h[j]) && h[j] != '>' && h[j] != '/') nm += static_cast<char>(std::tolower(static_cast<unsigned char>(h[j++])));
            const size_t e = h.find('>', j);
            i = e == std::string::npos ? n : e + 1;
            if (nm.empty()) continue;
            for (int k = static_cast<int>(st.size()) - 1; k >= 1; k--)
                if (st[k]->tag == nm) { closeTo(static_cast<size_t>(k)); break; }
            continue;
        }
        if (i + 1 >= n || !std::isalpha(static_cast<unsigned char>(h[i + 1]))) { addText("<", true); i++; continue; }
        // etiqueta de apertura
        size_t j = i + 1;
        std::string name;
        while (j < n && !isWs(h[j]) && h[j] != '>' && h[j] != '/') name += static_cast<char>(std::tolower(static_cast<unsigned char>(h[j++])));
        std::vector<std::pair<std::string, std::string>> attrs;
        bool selfClose = false;
        while (j < n && h[j] != '>') {
            while (j < n && (isWs(h[j]))) j++;
            if (j >= n || h[j] == '>') break;
            if (h[j] == '/') { selfClose = true; j++; continue; }
            selfClose = false;
            std::string an, av;
            while (j < n && !isWs(h[j]) && h[j] != '=' && h[j] != '>' && h[j] != '/') an += static_cast<char>(std::tolower(static_cast<unsigned char>(h[j++])));
            while (j < n && isWs(h[j])) j++;
            if (j < n && h[j] == '=') {
                j++;
                while (j < n && isWs(h[j])) j++;
                if (j < n && (h[j] == '"' || h[j] == '\'')) {
                    const char q = h[j++];
                    const size_t e = h.find(q, j);
                    const size_t end = e == std::string::npos ? n : e;
                    av = h.substr(j, end - j);
                    j = e == std::string::npos ? n : e + 1;
                } else {
                    while (j < n && !isWs(h[j]) && h[j] != '>') av += h[j++];
                }
                av = domDecodeEntities(av);
            }
            if (an.empty()) { if (j < n && h[j] != '>') j++; continue; }
            bool dup = false;
            for (const auto& a : attrs) if (a.first == an) { dup = true; break; }
            if (!dup) attrs.push_back({an, av});
        }
        i = j < n ? j + 1 : n;
        // cierres implicitos
        auto top = [&]() -> const std::string& { return st.back()->tag; };
        static const char* blockClosesP[] = {"p", "div", "ul", "ol", "table", "h1", "h2", "h3", "h4", "h5", "h6", "pre", "form", "blockquote", "hr", "section", "article",
                                             "header", "footer", "nav", "aside", "main", "fieldset", "address", "figure", "dl", "details", nullptr};
        for (int b = 0; blockClosesP[b]; b++)
            if (name == blockClosesP[b]) { if (st.size() > 1 && top() == "p") st.pop_back(); break; }
        if (name == "li") { int k = findOpen("li", "ul", "ol", "menu"); if (k > 0) closeTo(static_cast<size_t>(k)); }
        else if (name == "dt" || name == "dd") { int k = findOpen("dt", "dl", nullptr, nullptr); if (k < 0) k = findOpen("dd", "dl", nullptr, nullptr); if (k > 0) closeTo(static_cast<size_t>(k)); }
        else if (name == "option") { if (st.size() > 1 && top() == "option") st.pop_back(); }
        else if (name == "optgroup") { if (st.size() > 1 && top() == "option") st.pop_back(); if (st.size() > 1 && top() == "optgroup") st.pop_back(); }
        else if (name == "tr") { int k = findOpen("tr", "table", nullptr, nullptr); if (k > 0) closeTo(static_cast<size_t>(k)); }
        else if (name == "td" || name == "th") { int k = findOpen("td", "tr", "table", nullptr); if (k < 0) k = findOpen("th", "tr", "table", nullptr); if (k > 0) closeTo(static_cast<size_t>(k)); }
        else if (name == "tbody" || name == "thead" || name == "tfoot") { int k = findOpen("tbody", "table", nullptr, nullptr); if (k < 0) k = findOpen("thead", "table", nullptr, nullptr); if (k < 0) k = findOpen("tfoot", "table", nullptr, nullptr); if (k > 0) closeTo(static_cast<size_t>(k)); }
        if (nodes_.size() > 80000) { i = n; break; }
        DomNode* el = createElement(name);
        el->attrs = std::move(attrs);
        el->parent = st.back();
        st.back()->kids.push_back(el);
        if (isVoidTag(name) || selfClose) continue;
        if (isRawTag(name)) {
            // contenido hasta </name
            size_t e = i;
            while (true) {
                e = h.find("</", e);
                if (e == std::string::npos) break;
                if (e + 2 + name.size() <= n && lower(h.substr(e + 2, name.size())) == name) break;
                e += 2;
            }
            const size_t end = e == std::string::npos ? n : e;
            const std::string raw = h.substr(i, end - i);
            if (!raw.empty()) {
                DomNode* tn = createText((name == "textarea" || name == "title") ? domDecodeEntities(raw) : raw);
                tn->parent = el;
                el->kids.push_back(tn);
            }
            if (e == std::string::npos) { i = n; }
            else { const size_t gt = h.find('>', e); i = gt == std::string::npos ? n : gt + 1; }
            continue;
        }
        if (st.size() < 400) st.push_back(el);
    }
}

void Dom::ensureStructure() {
    DomNode* rt = root();
    DomNode* h = html();
    if (!h) {
        h = createElement("html");
        std::vector<DomNode*> old = rt->kids;
        rt->kids.clear();
        h->parent = rt;
        rt->kids.push_back(h);
        for (DomNode* k : old) { k->parent = h; h->kids.push_back(k); }
    }
    DomNode* hd = head();
    if (!hd) {
        hd = createElement("head");
        hd->parent = h;
        h->kids.insert(h->kids.begin(), hd);
    }
    DomNode* bd = body();
    if (!bd) {
        bd = createElement("body");
        bd->parent = h;
        std::vector<DomNode*> keep;
        std::vector<DomNode*> move;
        for (DomNode* k : h->kids) {
            if (k == hd) keep.push_back(k);
            else move.push_back(k);
        }
        h->kids = keep;
        h->kids.push_back(bd);
        for (DomNode* k : move) { k->parent = bd; bd->kids.push_back(k); }
    }
}

void Dom::parse(const std::string& htmlText) {
    nodes_.resize(1);
    root()->kids.clear();
    extraCss_.clear();
    parseInto(root(), htmlText);
    ensureStructure();
    dirty = true;
    styleDirty_ = true;
}

DomNode* Dom::clone(DomNode* n, bool deep) {
    DomNode* c = newNode(n->type);
    c->tag = n->tag;
    c->text = n->text;
    c->attrs = n->attrs;
    if (deep)
        for (DomNode* k : n->kids) {
            DomNode* kc = clone(k, true);
            kc->parent = c;
            c->kids.push_back(kc);
        }
    return c;
}

void Dom::touchStyle(const DomNode* n) {
    if (styleDirty_ || !n) return;
    if (n->tag == "style" || n->tag == "link" || (n->parent && n->parent->tag == "style")) { styleDirty_ = true; return; }
    std::vector<const DomNode*> st{n};
    int budget = 2000;
    while (!st.empty() && budget-- > 0) {
        const DomNode* x = st.back();
        st.pop_back();
        if (x->tag == "style" || x->tag == "link") { styleDirty_ = true; return; }
        for (const DomNode* k : x->kids) st.push_back(k);
    }
}

void Dom::remove(DomNode* c) {
    if (!c || !c->parent) return;
    DomNode* p = c->parent;
    p->kids.erase(std::remove(p->kids.begin(), p->kids.end(), c), p->kids.end());
    c->parent = nullptr;
    touchStyle(c);
    dirty = true;
}
static bool isAncestor(const DomNode* a, const DomNode* n) {
    for (const DomNode* p = n; p; p = p->parent) if (p == a) return true;
    return false;
}
void Dom::insertBefore(DomNode* parent, DomNode* child, DomNode* ref) {
    if (!parent || !child || child == parent || isAncestor(child, parent)) return;
    if (child->type == DomNode::FRAGMENT) {
        std::vector<DomNode*> ks = child->kids;
        child->kids.clear();
        for (DomNode* k : ks) { k->parent = nullptr; insertBefore(parent, k, ref); }
        return;
    }
    if (child->parent) remove(child);
    size_t pos = parent->kids.size();
    if (ref) {
        auto it = std::find(parent->kids.begin(), parent->kids.end(), ref);
        if (it != parent->kids.end()) pos = static_cast<size_t>(it - parent->kids.begin());
    }
    parent->kids.insert(parent->kids.begin() + static_cast<long>(pos), child);
    child->parent = parent;
    touchStyle(child);
    dirty = true;
}
void Dom::append(DomNode* parent, DomNode* child) { insertBefore(parent, child, nullptr); }

void Dom::setText(DomNode* n, const std::string& s) {
    if (!n) return;
    if (n->type == DomNode::TEXT || n->type == DomNode::COMMENT) { n->text = s; dirty = true; touchStyle(n); return; }
    for (DomNode* k : n->kids) k->parent = nullptr;
    n->kids.clear();
    if (!s.empty()) { DomNode* t = createText(s); t->parent = n; n->kids.push_back(t); }
    if (n->tag == "style") styleDirty_ = true;
    dirty = true;
}
void Dom::setInnerHtml(DomNode* n, const std::string& h) {
    if (!n) return;
    for (DomNode* k : n->kids) k->parent = nullptr;
    n->kids.clear();
    if (isRawTag(n->tag)) { if (!h.empty()) { DomNode* t = createText(h); t->parent = n; n->kids.push_back(t); } }
    else parseInto(n, h);
    styleDirty_ = true;
    dirty = true;
}
void Dom::setAttr(DomNode* n, const std::string& name, const std::string& v) {
    if (!n || n->type != DomNode::ELEMENT) return;
    const std::string nm = lower(name);
    for (auto& a : n->attrs) if (a.first == nm) { a.second = v; dirty = true; return; }
    n->attrs.push_back({nm, v});
    dirty = true;
}
void Dom::removeAttr(DomNode* n, const std::string& name) {
    if (!n) return;
    const std::string nm = lower(name);
    n->attrs.erase(std::remove_if(n->attrs.begin(), n->attrs.end(), [&](const std::pair<std::string, std::string>& a) { return a.first == nm; }), n->attrs.end());
    dirty = true;
}

std::string Dom::textContent(const DomNode* n) {
    if (!n) return "";
    if (n->type == DomNode::TEXT || n->type == DomNode::COMMENT) return n->text;
    std::string o;
    std::vector<const DomNode*> st{n};
    // recorrido en orden de documento sin recursion
    std::vector<std::pair<const DomNode*, size_t>> stack{{n, 0}};
    while (!stack.empty()) {
        auto& top = stack.back();
        if (top.second >= top.first->kids.size()) { stack.pop_back(); continue; }
        const DomNode* k = top.first->kids[top.second++];
        if (k->type == DomNode::TEXT) o += k->text;
        else if (k->type == DomNode::ELEMENT || k->type == DomNode::FRAGMENT) stack.push_back({k, 0});
    }
    return o;
}

DomNode* Dom::byId(const std::string& id) {
    std::vector<DomNode*> st{root()};
    while (!st.empty()) {
        DomNode* x = st.back();
        st.pop_back();
        if (x->type == DomNode::ELEMENT) { const std::string* v = getAttr(x, "id"); if (v && *v == id) return x; }
        for (size_t k = x->kids.size(); k-- > 0;) st.push_back(x->kids[k]);  // orden de documento
    }
    return nullptr;
}

static std::vector<Complex>* parseSel(const std::string& s, std::vector<Complex>& store, bool* ok) {
    SelParser p(s);
    *ok = p.list(store);
    return &store;
}
std::vector<DomNode*> Dom::query(DomNode* rootNode, const std::string& selector, bool firstOnly, bool* syntaxOk) {
    std::vector<DomNode*> res;
    std::vector<Complex> sels;
    bool ok = false;
    parseSel(selector, sels, &ok);
    if (syntaxOk) *syntaxOk = ok;
    if (!ok || !rootNode) return res;
    std::vector<std::pair<DomNode*, size_t>> stack{{rootNode, 0}};
    while (!stack.empty()) {
        auto& top = stack.back();
        if (top.second >= top.first->kids.size()) { stack.pop_back(); continue; }
        DomNode* k = top.first->kids[top.second++];
        if (k->type != DomNode::ELEMENT) continue;
        for (const auto& cx : sels)
            if (matchComplex(k, cx, static_cast<int>(cx.parts.size()) - 1)) { res.push_back(k); break; }
        if (firstOnly && !res.empty()) return res;
        stack.push_back({k, 0});
    }
    return res;
}
bool Dom::matches(DomNode* n, const std::string& selector) {
    std::vector<Complex> sels;
    bool ok = false;
    parseSel(selector, sels, &ok);
    if (!ok || !n) return false;
    for (const auto& cx : sels) if (matchComplex(n, cx, static_cast<int>(cx.parts.size()) - 1)) return true;
    return false;
}
DomNode* Dom::closest(DomNode* n, const std::string& selector) {
    for (DomNode* p = n; p; p = p->parent) if (p->type == DomNode::ELEMENT && matches(p, selector)) return p;
    return nullptr;
}

std::vector<std::string> Dom::linkedStyleHrefs() {
    std::vector<std::string> v;
    bool ok;
    for (DomNode* l : query(root(), "link", false, &ok)) {
        const std::string* rel = getAttr(l, "rel");
        const std::string* href = getAttr(l, "href");
        const std::string* media = getAttr(l, "media");
        if (!rel || !href || lower(*rel).find("stylesheet") == std::string::npos) continue;
        if (media && lower(*media).find("print") != std::string::npos) continue;
        v.push_back(*href);
    }
    return v;
}
void Dom::addStyleSheet(const std::string& css) { extraCss_.push_back(css); styleDirty_ = true; }

void Dom::rebuildRules() {
    rules_ = std::make_shared<Rules>();
    int order = 0;
    for (const auto& c : extraCss_) parseCss(c, *rules_, order);
    bool ok;
    for (DomNode* s : query(root(), "style", false, &ok)) {
        const std::string* media = getAttr(s, "media");
        if (media && lower(*media).find("print") != std::string::npos) continue;
        parseCss(textContent(s), *rules_, order);
    }
    styleDirty_ = false;
}

// ------------------------------------------------------------------ serializacion
namespace {
struct Computed { bool hidden = false; std::string color; };
}
static Computed computeStyle(const DomNode* n, const Dom::Rules& R) {
    int bestDisp = -1, bestVis = -1, bestCol = -1;  // clave = (importante<<30)|especificidad*... + orden
    std::string disp, col;
    int vis = -1;
    auto key = [](bool imp, int spec, int order) { return (static_cast<long long>(imp) << 40) + static_cast<long long>(spec) * 100000 + order; };
    long long kd = -1, kv = -1, kc = -1;
    (void)bestDisp; (void)bestVis; (void)bestCol;
    auto consider = [&](int idx) {
        const Dom::Rules::Rule& r = R.all[idx];
        if (!matchComplex(n, r.sel, static_cast<int>(r.sel.parts.size()) - 1)) return;
        if (!r.display.empty()) { const long long k = key(r.displayImp, r.spec, r.order); if (k > kd) { kd = k; disp = r.display; } }
        if (r.vis >= 0) { const long long k = key(r.visImp, r.spec, r.order); if (k > kv) { kv = k; vis = r.vis; } }
        if (!r.color.empty()) { const long long k = key(r.colorImp, r.spec, r.order); if (k > kc) { kc = k; col = r.color; } }
    };
    if (!R.all.empty()) {
        const std::string* id = Dom::getAttr(n, "id");
        if (id && !id->empty()) { auto it = R.byId.find(*id); if (it != R.byId.end()) for (int x : it->second) consider(x); }
        const std::string* cl = Dom::getAttr(n, "class");
        if (cl) {
            size_t p = 0;
            while (p < cl->size()) {
                while (p < cl->size() && isWs((*cl)[p])) p++;
                size_t q = p;
                while (q < cl->size() && !isWs((*cl)[q])) q++;
                if (q > p) { auto it = R.byClass.find(cl->substr(p, q - p)); if (it != R.byClass.end()) for (int x : it->second) consider(x); }
                p = q;
            }
        }
        { auto it = R.byTag.find(n->tag); if (it != R.byTag.end()) for (int x : it->second) consider(x); }
        for (int x : R.other) consider(x);
    }
    // estilo en linea (gana salvo !important de hoja)
    const std::string* st = Dom::getAttr(n, "style");
    if (st) {
        Dom::Rules::Rule r;
        parseDecls(*st, r);
        if (!r.display.empty() && !(kd >= (1LL << 40) && !r.displayImp)) disp = r.display;
        if (r.vis >= 0 && !(kv >= (1LL << 40) && !r.visImp)) vis = r.vis;
        if (!r.color.empty() && !(kc >= (1LL << 40) && !r.colorImp)) col = r.color;
    }
    Computed c;
    if (!disp.empty()) c.hidden = disp == "none";
    else c.hidden = Dom::getAttr(n, "hidden") != nullptr && n->tag != "input";
    if (vis == 1) c.hidden = true;
    c.color = col;
    return c;
}

// mode: 0 = vista (para maquetar), 1 = crudo (innerHTML/outerHTML: incluye script/style, sin CSS)
void Dom::ser(const DomNode* n, std::string& out, int mode, bool dropNoscript) {
    switch (n->type) {
        case DomNode::TEXT:
            if (n->parent && (n->parent->tag == "script" || n->parent->tag == "style")) { if (mode == 1) out += n->text; }
            else out += domEscapeText(n->text);
            return;
        case DomNode::COMMENT: return;
        case DomNode::DOCUMENT:
        case DomNode::FRAGMENT:
            for (const DomNode* k : n->kids) ser(k, out, mode, dropNoscript);
            return;
        case DomNode::ELEMENT: break;
    }
    if (mode == 0) {
        if (n->tag == "script" || n->tag == "style") return;
        if (dropNoscript && n->tag == "noscript") return;
    }
    Computed cs;
    if (mode == 0) { cs = computeStyle(n, *rules_); if (cs.hidden) return; }
    out += '<';
    out += n->tag;
    bool wroteStyle = false;
    for (const auto& a : n->attrs) {
        if (mode == 0 && a.first == "hidden") continue;
        out += ' ';
        out += a.first;
        std::string v = a.second;
        if (mode == 0 && a.first == "style" && !cs.color.empty()) { v += ";color:" + cs.color; wroteStyle = true; }
        if (!v.empty()) { out += "=\""; out += domEscapeAttr(v); out += '"'; }
    }
    if (mode == 0 && !wroteStyle && !cs.color.empty()) { out += " style=\"color:"; out += domEscapeAttr(cs.color); out += '"'; }
    out += '>';
    if (isVoidTag(n->tag)) return;
    for (const DomNode* k : n->kids) ser(k, out, mode, dropNoscript);
    out += "</";
    out += n->tag;
    out += '>';
}

std::string Dom::serialize(bool applyCss, bool dropNoscript) {
    if (applyCss && styleDirty_) rebuildRules();
    if (!applyCss) { rules_ = std::make_shared<Rules>(); styleDirty_ = true; }
    std::string out;
    out.reserve(32768);
    ser(root(), out, 0, dropNoscript);
    dirty = false;
    return out;
}
std::string Dom::innerHtml(const DomNode* n) {
    std::string o;
    if (n) for (const DomNode* k : n->kids) ser(k, o, 1, false);
    return o;
}
std::string Dom::outerHtml(const DomNode* n) {
    std::string o;
    if (n) ser(n, o, 1, false);
    return o;
}

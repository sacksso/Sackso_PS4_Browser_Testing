// Capa de pagina avanzada: DOM + CSS lite + JavaScript (ver jsx.h).
#include "jsx.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <memory>
#include <vector>

#include "dom.h"

#ifndef PSB_NO_JS
extern "C" {
#include "quickjs.h"
}
#include "jsprelude.h"
#endif

static JsHost g_host;
void jsx_set_host(const JsHost& h) { g_host = h; }
static void hlog(const std::string& s) { if (g_host.log) g_host.log(s); }
static uint32_t hnow() { return g_host.nowMs ? g_host.nowMs() : 0; }
bool jsx_has_engine() {
#ifndef PSB_NO_JS
    return true;
#else
    return false;
#endif
}

namespace {
struct Timer {
    int id = 0;
    uint32_t due = 0;
    int interval = -1;  // >= 0: repetir
    bool raf = false;
    int errors = 0;     // errores seguidos de un intervalo: a los 3 se cancela
#ifndef PSB_NO_JS
    JSValue fn = JS_UNDEFINED;
#endif
};

struct Session {
    Dom dom;
    std::string url;
    void* owner = nullptr;
    bool js = false;
    uint32_t t0 = 0, deadline = 0, lastSer = 0;
    int nextTimer = 1;
    int fetches = 0;
    int domFetchBudget = 12;
    int timerErrLogged = 0;
    DomNode* curScript = nullptr;
    std::vector<Timer> timers;
    std::string status;
#ifndef PSB_NO_JS
    JSRuntime* rt = nullptr;
    JSContext* ctx = nullptr;
#endif
    ~Session();
};
Session* g_s = nullptr;
}  // namespace

#ifndef PSB_NO_JS
Session::~Session() {
    for (Timer& t : timers) if (ctx) JS_FreeValue(ctx, t.fn);
    timers.clear();
    if (ctx) JS_FreeContext(ctx);
    if (rt) JS_FreeRuntime(rt);
}
#else
Session::~Session() {}
#endif

#ifndef PSB_NO_JS
// ------------------------------------------------------------------ utilidades QuickJS
static std::string jstr(JSContext* c, JSValueConst v) {
    size_t n = 0;
    const char* s = JS_ToCStringLen(c, &n, v);
    std::string r = s ? std::string(s, n) : std::string();
    if (s) JS_FreeCString(c, s);
    return r;
}
static int jint(JSContext* c, JSValueConst v, int def = -1) {
    int32_t x = def;
    if (JS_ToInt32(c, &x, v) != 0) return def;
    return x;
}
static JSValue jnewstr(JSContext* c, const std::string& s) { return JS_NewStringLen(c, s.data(), s.size()); }
static void logException(JSContext* c, const char* what) {
    JSValue ex = JS_GetException(c);
    std::string msg = jstr(c, ex);
    JSValue st = JS_GetPropertyStr(c, ex, "stack");
    std::string stack = JS_IsUndefined(st) ? "" : jstr(c, st);
    JS_FreeValue(c, st);
    JS_FreeValue(c, ex);
    if (msg.size() > 400) msg.resize(400);
    if (stack.size() > 300) stack.resize(300);
    for (char& ch : stack) if (ch == '\n') ch = '|';
    hlog(std::string("[js] ERROR en ") + what + ": " + msg + (stack.empty() ? "" : " @ " + stack));
}
static DomNode* nodeArg(JSContext* c, JSValueConst v) { return g_s ? g_s->dom.node(jint(c, v, -1)) : nullptr; }
static int idOrNeg(const DomNode* n) { return n ? n->id : -1; }

#define FN(name) static JSValue name(JSContext* c, JSValueConst, int argc, JSValueConst* argv __attribute__((unused)))
#define NEED(n) if (!g_s || argc < (n)) return JS_UNDEFINED

// ---- __d: primitivas del DOM
FN(d_type) { NEED(1); DomNode* n = nodeArg(c, argv[0]); return JS_NewInt32(c, n ? static_cast<int>(n->type) : 0); }
FN(d_tag) { NEED(1); DomNode* n = nodeArg(c, argv[0]); return jnewstr(c, n ? n->tag : ""); }
FN(d_parent) { NEED(1); DomNode* n = nodeArg(c, argv[0]); return JS_NewInt32(c, n ? idOrNeg(n->parent) : -1); }
FN(d_first) { NEED(1); DomNode* n = nodeArg(c, argv[0]); return JS_NewInt32(c, n && !n->kids.empty() ? n->kids.front()->id : -1); }
FN(d_last) { NEED(1); DomNode* n = nodeArg(c, argv[0]); return JS_NewInt32(c, n && !n->kids.empty() ? n->kids.back()->id : -1); }
static int sibling(DomNode* n, int dir) {
    if (!n || !n->parent) return -1;
    auto& k = n->parent->kids;
    for (size_t i = 0; i < k.size(); i++)
        if (k[i] == n) {
            const long j = static_cast<long>(i) + dir;
            return j >= 0 && j < static_cast<long>(k.size()) ? k[static_cast<size_t>(j)]->id : -1;
        }
    return -1;
}
FN(d_next) { NEED(1); return JS_NewInt32(c, sibling(nodeArg(c, argv[0]), 1)); }
FN(d_prev) { NEED(1); return JS_NewInt32(c, sibling(nodeArg(c, argv[0]), -1)); }
FN(d_data) { NEED(1); DomNode* n = nodeArg(c, argv[0]); return jnewstr(c, n ? n->text : ""); }
FN(d_setData) { NEED(2); DomNode* n = nodeArg(c, argv[0]); if (n) g_s->dom.setText(n, jstr(c, argv[1])); return JS_UNDEFINED; }
FN(d_textContent) { NEED(1); return jnewstr(c, Dom::textContent(nodeArg(c, argv[0]))); }
FN(d_setText) { NEED(2); DomNode* n = nodeArg(c, argv[0]); if (n) g_s->dom.setText(n, jstr(c, argv[1])); return JS_UNDEFINED; }
FN(d_getAttr) {
    NEED(2);
    DomNode* n = nodeArg(c, argv[0]);
    if (!n) return JS_UNDEFINED;
    const std::string* v = Dom::getAttr(n, jstr(c, argv[1]));
    return v ? jnewstr(c, *v) : JS_UNDEFINED;
}
FN(d_setAttr) { NEED(3); g_s->dom.setAttr(nodeArg(c, argv[0]), jstr(c, argv[1]), jstr(c, argv[2])); return JS_UNDEFINED; }
FN(d_rmAttr) { NEED(2); g_s->dom.removeAttr(nodeArg(c, argv[0]), jstr(c, argv[1])); return JS_UNDEFINED; }
FN(d_attrNames) {
    NEED(1);
    DomNode* n = nodeArg(c, argv[0]);
    JSValue a = JS_NewArray(c);
    if (n) for (size_t i = 0; i < n->attrs.size(); i++) JS_SetPropertyUint32(c, a, static_cast<uint32_t>(i), jnewstr(c, n->attrs[i].first));
    return a;
}
FN(d_create) { NEED(1); return JS_NewInt32(c, g_s->dom.createElement(jstr(c, argv[0]))->id); }
FN(d_createText) { NEED(1); return JS_NewInt32(c, g_s->dom.createText(jstr(c, argv[0]))->id); }
FN(d_createFrag) { NEED(0); return JS_NewInt32(c, g_s->dom.createFragment()->id); }
FN(d_clone) { NEED(2); DomNode* n = nodeArg(c, argv[0]); return JS_NewInt32(c, n ? g_s->dom.clone(n, JS_ToBool(c, argv[1]) != 0)->id : -1); }
FN(d_append) { NEED(2); g_s->dom.append(nodeArg(c, argv[0]), nodeArg(c, argv[1])); return JS_UNDEFINED; }
FN(d_insertBefore) { NEED(3); g_s->dom.insertBefore(nodeArg(c, argv[0]), nodeArg(c, argv[1]), nodeArg(c, argv[2])); return JS_UNDEFINED; }
FN(d_remove) { NEED(1); g_s->dom.remove(nodeArg(c, argv[0])); return JS_UNDEFINED; }
FN(d_inner) { NEED(1); return jnewstr(c, g_s->dom.innerHtml(nodeArg(c, argv[0]))); }
FN(d_setInner) { NEED(2); g_s->dom.setInnerHtml(nodeArg(c, argv[0]), jstr(c, argv[1])); return JS_UNDEFINED; }
FN(d_outer) { NEED(1); return jnewstr(c, g_s->dom.outerHtml(nodeArg(c, argv[0]))); }
FN(d_byId) { NEED(1); return JS_NewInt32(c, idOrNeg(g_s->dom.byId(jstr(c, argv[0])))); }
FN(d_qsa) {
    NEED(3);
    DomNode* root = nodeArg(c, argv[0]);
    bool ok = false;
    std::vector<DomNode*> r = g_s->dom.query(root, jstr(c, argv[1]), JS_ToBool(c, argv[2]) != 0, &ok);
    if (!ok) return JS_NULL;
    JSValue a = JS_NewArray(c);
    for (size_t i = 0; i < r.size(); i++) JS_SetPropertyUint32(c, a, static_cast<uint32_t>(i), JS_NewInt32(c, r[i]->id));
    return a;
}
FN(d_matches) { NEED(2); DomNode* n = nodeArg(c, argv[0]); return JS_NewBool(c, n && g_s->dom.matches(n, jstr(c, argv[1]))); }
FN(d_closest) { NEED(2); DomNode* n = nodeArg(c, argv[0]); return JS_NewInt32(c, n ? idOrNeg(g_s->dom.closest(n, jstr(c, argv[1]))) : -1); }
FN(d_doc) { NEED(0); return JS_NewInt32(c, 0); }
FN(d_html) { NEED(0); return JS_NewInt32(c, idOrNeg(g_s->dom.html())); }
FN(d_head) { NEED(0); return JS_NewInt32(c, idOrNeg(g_s->dom.head())); }
FN(d_body) { NEED(0); return JS_NewInt32(c, idOrNeg(g_s->dom.body())); }
FN(d_currentScript) { NEED(0); return JS_NewInt32(c, idOrNeg(g_s->curScript)); }
FN(d_write) {
    NEED(1);
    Dom& d = g_s->dom;
    DomNode* tmp = d.createElement("div");
    d.setInnerHtml(tmp, jstr(c, argv[0]));
    std::vector<DomNode*> kids = tmp->kids;
    DomNode* cs = g_s->curScript;
    DomNode* par = cs && cs->parent ? cs->parent : d.body();
    DomNode* ref = nullptr;
    if (cs && cs->parent) { const int nx = sibling(cs, 1); ref = nx >= 0 ? d.node(nx) : nullptr; }
    if (!par) return JS_UNDEFINED;
    for (DomNode* k : kids) d.insertBefore(par, k, ref);
    return JS_UNDEFINED;
}

// ---- __n: sistema
FN(n_log) {
    NEED(2);
    const int lv = jint(c, argv[0], 0);
    std::string m = jstr(c, argv[1]);
    if (m.size() > 500) m.resize(500);
    for (char& ch : m) if (ch == '\n') ch = ' ';
    hlog(std::string(lv >= 2 ? "[js] ERROR: " : (lv == 1 ? "[js] warn: " : "[js] ")) + m);
    return JS_UNDEFINED;
}
FN(n_now) { NEED(0); return JS_NewFloat64(c, static_cast<double>(hnow() - g_s->t0)); }
static JSValue addTimer(JSContext* c, JSValueConst fn, int ms, bool repeat, bool raf) {
    if (!JS_IsFunction(c, fn) || g_s->timers.size() > 400) return JS_NewInt32(c, 0);
    Timer t;
    t.id = g_s->nextTimer++;
    t.due = hnow() + static_cast<uint32_t>(raf ? 16 : std::max(0, ms));
    t.interval = repeat ? std::max(ms, 4) : -1;
    t.raf = raf;
    t.fn = JS_DupValue(c, fn);
    g_s->timers.push_back(t);
    return JS_NewInt32(c, t.id);
}
FN(n_setTimer) { NEED(3); return addTimer(c, argv[0], jint(c, argv[1], 0), JS_ToBool(c, argv[2]) != 0, false); }
FN(n_raf) { NEED(1); return addTimer(c, argv[0], 16, false, true); }
FN(n_clearTimer) {
    NEED(1);
    const int id = jint(c, argv[0], 0);
    for (size_t i = 0; i < g_s->timers.size(); i++)
        if (g_s->timers[i].id == id) { JS_FreeValue(c, g_s->timers[i].fn); g_s->timers[i].fn = JS_UNDEFINED; g_s->timers[i].id = -1; break; }
    return JS_UNDEFINED;
}
FN(n_url) { NEED(0); return jnewstr(c, g_s->url); }
FN(n_cookieGet) { NEED(0); if (!g_host.getCookies) return JS_NULL; return jnewstr(c, g_host.getCookies(g_s->url)); }
FN(n_cookieSet) { NEED(1); if (g_host.setCookie) g_host.setCookie(g_s->url, jstr(c, argv[0])); return JS_UNDEFINED; }
FN(n_ua) { NEED(0); return jnewstr(c, g_host.userAgent); }
FN(n_resolve) { NEED(2); return jnewstr(c, g_host.resolve ? g_host.resolve(jstr(c, argv[0]), jstr(c, argv[1])) : jstr(c, argv[1])); }
FN(n_fetchSync) {
    NEED(2);
    std::string url = jstr(c, argv[0]);
    const std::string method = jstr(c, argv[1]);
    if (url.empty() || g_s->fetches >= 10 || !g_host.fetch || (method != "GET" && method != "get" && method != "HEAD")) return JS_NULL;
    if (g_host.resolve) url = g_host.resolve(g_s->url, url);
    if (url.compare(0, 4, "http") != 0) return JS_NULL;
    g_s->fetches++;
    std::string body, fin, ct;
    int status = 0;
    const uint32_t t = hnow();
    const bool ok = g_host.fetch(url, 1024 * 1024, body, fin, status, ct);
    g_s->deadline += hnow() - t;  // el tiempo de red no cuenta para el limite del script
    hlog("[js] fetch " + url + (ok ? " -> " + std::to_string(status) + " (" + std::to_string(body.size()) + " bytes)" : " FALLO"));
    if (!ok) return JS_NULL;
    JSValue o = JS_NewObject(c);
    JS_SetPropertyStr(c, o, "status", JS_NewInt32(c, status));
    JS_SetPropertyStr(c, o, "body", jnewstr(c, body));
    JS_SetPropertyStr(c, o, "url", jnewstr(c, fin));
    JS_SetPropertyStr(c, o, "type", jnewstr(c, ct));
    return o;
}

static void regFns(JSContext* c, JSValueConst obj, const char* nm, JSCFunction* f, int len) { JS_SetPropertyStr(c, obj, nm, JS_NewCFunction(c, f, nm, len)); }

static int interruptCb(JSContext*, void*) { return g_s && hnow() > g_s->deadline ? 1 : 0; }

// Asignador propio: guarda el tamano en una cabecera de 16 bytes. Asi QuickJS no necesita malloc_usable_size (que en OpenOrbis/FreeBSD
// no esta garantizado) y el limite de memoria (JS_SetMemoryLimit) sigue funcionando.
static void* psbAlloc(size_t n) {
    uint8_t* p = static_cast<uint8_t*>(malloc(n + 16));
    if (!p) return nullptr;
    *reinterpret_cast<size_t*>(p) = n;
    return p + 16;
}
static void* psbCalloc(void*, size_t cnt, size_t sz) {
    if (sz && cnt > (static_cast<size_t>(-1) - 16) / sz) return nullptr;
    void* p = psbAlloc(cnt * sz);
    if (p) memset(p, 0, cnt * sz);
    return p;
}
static void* psbMalloc(void*, size_t n) { return psbAlloc(n); }
static void psbFree(void*, void* p) { if (p) free(static_cast<uint8_t*>(p) - 16); }
static void* psbRealloc(void*, void* p, size_t n) {
    if (!p) return psbAlloc(n);
    if (n == 0) { psbFree(nullptr, p); return nullptr; }
    uint8_t* q = static_cast<uint8_t*>(realloc(static_cast<uint8_t*>(p) - 16, n + 16));
    if (!q) return nullptr;
    *reinterpret_cast<size_t*>(q) = n;
    return q + 16;
}
static size_t psbUsable(const void* p) { return p ? *reinterpret_cast<const size_t*>(static_cast<const uint8_t*>(p) - 16) : 0; }

static void setupEngine(Session* s) {
    static const JSMallocFunctions mf = {psbCalloc, psbMalloc, psbFree, psbRealloc, psbUsable};
    s->rt = JS_NewRuntime2(&mf, nullptr);
    JS_SetMemoryLimit(s->rt, 64u * 1024 * 1024);
    JS_SetMaxStackSize(s->rt, 512 * 1024);
    JS_SetInterruptHandler(s->rt, interruptCb, nullptr);
    s->ctx = JS_NewContext(s->rt);
    JSContext* c = s->ctx;
    JSValue G = JS_GetGlobalObject(c);
    JSValue D = JS_NewObject(c), N = JS_NewObject(c);
#define R(o, nm, len) regFns(c, o, #nm, d_##nm, len)
    R(D, type, 1); R(D, tag, 1); R(D, parent, 1); R(D, first, 1); R(D, last, 1); R(D, next, 1); R(D, prev, 1); R(D, data, 1); R(D, setData, 2);
    R(D, textContent, 1); R(D, setText, 2); R(D, getAttr, 2); R(D, setAttr, 3); R(D, rmAttr, 2); R(D, attrNames, 1); R(D, create, 1);
    R(D, createText, 1); R(D, createFrag, 0); R(D, clone, 2); R(D, append, 2); R(D, insertBefore, 3); R(D, remove, 1); R(D, inner, 1);
    R(D, setInner, 2); R(D, outer, 1); R(D, byId, 1); R(D, qsa, 3); R(D, matches, 2); R(D, closest, 2); R(D, doc, 0); R(D, html, 0);
    R(D, head, 0); R(D, body, 0); R(D, currentScript, 0); R(D, write, 1);
#undef R
#define R(o, nm, len) regFns(c, o, #nm, n_##nm, len)
    R(N, log, 2); R(N, now, 0); R(N, setTimer, 3); R(N, raf, 1); R(N, clearTimer, 1); R(N, url, 0); R(N, cookieGet, 0); R(N, cookieSet, 1); R(N, ua, 0); R(N, resolve, 2); R(N, fetchSync, 2);
#undef R
    JS_SetPropertyStr(c, G, "__d", D);
    JS_SetPropertyStr(c, G, "__n", N);
    JS_FreeValue(c, G);
}

static bool evalCode(Session* s, const std::string& code, const char* name) {
    JSValue r = JS_Eval(s->ctx, code.c_str(), code.size(), name, JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) { logException(s->ctx, name); return false; }
    JS_FreeValue(s->ctx, r);
    return true;
}
static void drainJobs(Session* s) {
    for (int i = 0; i < 2000; i++) {
        JSContext* c1 = nullptr;
        const int r = JS_ExecutePendingJob(s->rt, &c1);
        if (r == 0) break;
        if (r < 0) { logException(c1 ? c1 : s->ctx, "microtarea"); break; }
    }
}
#endif  // PSB_NO_JS

// ------------------------------------------------------------------ carga de pagina
static bool isJsType(const std::string* t) {
    if (!t) return true;
    std::string v = *t;
    for (auto& ch : v) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    while (!v.empty() && v.back() == ' ') v.pop_back();
    return v.empty() || v == "text/javascript" || v == "application/javascript" || v == "text/ecmascript" || v == "application/ecmascript" || v == "text/jscript" || v == "application/x-javascript";
}

std::string jsx_load(const std::string& url, const std::string& html, bool runJs, JsxInfo* info) {
    JsxInfo local;
    JsxInfo& I = info ? *info : local;
    I = JsxInfo();
    I.htmlIn = static_cast<int>(html.size());
    delete g_s;
    g_s = nullptr;
    Session* s = new Session();
    g_s = s;
    s->url = url;
    s->t0 = hnow();
    s->deadline = s->t0 + 8000;
    s->dom.parse(html);
    I.domOk = true;
    // hojas de estilo externas
    if (g_host.fetch) {
        int n = 0;
        for (const std::string& href : s->dom.linkedStyleHrefs()) {
            if (n >= 5) break;
            const std::string abs = g_host.resolve ? g_host.resolve(url, href) : href;
            if (abs.compare(0, 4, "http") != 0) continue;
            if (g_host.blocked && g_host.blocked(abs)) continue;
            std::string body, fin, ct;
            int status = 0;
            if (g_host.fetch(abs, 400 * 1024, body, fin, status, ct) && status == 200 && !body.empty()) { s->dom.addStyleSheet(body); n++; hlog("[css] " + abs + " (" + std::to_string(body.size()) + " bytes)"); }
            else hlog("[css] fallo " + abs);
        }
        I.cssFiles = n;
    }
    bool engine = false;
#ifndef PSB_NO_JS
    if (runJs) {
        setupEngine(s);
        s->js = true;
        engine = true;
        if (!evalCode(s, kJsPrelude, "prelude")) { hlog("[js] el prelude fallo: sin JavaScript en esta pagina"); engine = false; s->js = false; }
    }
    if (engine) {
        I.jsRan = true;
        std::vector<DomNode*> scripts = s->dom.query(s->dom.root(), "script", false, nullptr);
        int ext = 0;
        for (DomNode* sc : scripts) {
            if (hnow() - s->t0 > 9000) { hlog("[js] presupuesto de carga agotado: se omiten los scripts restantes"); break; }
            const std::string* ty = Dom::getAttr(sc, "type");
            if (ty && *ty == "module") continue;  // modulos ES: sin cargador (pendiente)
            if (!isJsType(ty)) continue;           // plantillas, JSON, etc.
            std::string code;
            std::string name = "inline";
            const std::string* src = Dom::getAttr(sc, "src");
            if (src && !src->empty()) {
                if (ext >= g_s->domFetchBudget || !g_host.fetch) continue;
                const std::string abs = g_host.resolve ? g_host.resolve(url, *src) : *src;
                if (abs.compare(0, 4, "http") != 0) continue;
                if (g_host.blocked && g_host.blocked(abs)) { hlog("[js] bloqueado (publicidad/rastreo): " + abs); continue; }
                std::string fin, ct;
                int status = 0;
                const uint32_t t = hnow();
                const bool ok = g_host.fetch(abs, 1536 * 1024, code, fin, status, ct);
                s->deadline += hnow() - t;
                ext++;
                if (!ok || status != 200) { hlog("[js] no se pudo bajar " + abs); continue; }
                name = abs.size() > 60 ? abs.substr(abs.size() - 60) : abs;
                hlog("[js] script " + abs + " (" + std::to_string(code.size()) + " bytes)");
            } else {
                code = Dom::textContent(sc);
            }
            if (code.empty()) continue;
            s->curScript = sc;
            s->deadline = hnow() + 2500;
            if (evalCode(s, code, name.c_str())) I.scripts++; else I.scriptErrors++;
            drainJobs(s);
            s->curScript = nullptr;
        }
        s->deadline = hnow() + 2500;
        evalCode(s, "__setReady('interactive');__fire('document','DOMContentLoaded');", "dcl");
        drainJobs(s);
        evalCode(s, "__setReady('complete');__fire('window','load');", "load");
        drainJobs(s);
    }
#else
    (void)runJs;
#endif
    std::string out = s->dom.serialize(true, engine);
    I.htmlOut = static_cast<int>(out.size());
    if (out.size() < 40) { hlog("[dom] resultado vacio: se usa el HTML original"); out = html; I.htmlOut = I.htmlIn; }
    s->lastSer = hnow();
    I.ms = hnow() - s->t0;
    char buf[200];
    snprintf(buf, sizeof(buf), "DOM ok, %d scripts (%d con error), %d CSS externos, %d -> %d bytes, %u ms", I.scripts, I.scriptErrors, I.cssFiles, I.htmlIn, I.htmlOut, static_cast<unsigned>(I.ms));
    s->status = buf;
    hlog(std::string("[jsx] ") + buf);
    if (!engine) { delete g_s; g_s = nullptr; }  // sin JS no hace falta mantener la sesion
    return out;
}

void jsx_attach(void* owner) { if (g_s) g_s->owner = owner; }
void jsx_close(void* owner) {
    if (!g_s) return;
    if (owner && g_s->owner != owner) return;  // una sesion todavia sin dueno (recien cargada) no se cierra por nombre
    delete g_s;
    g_s = nullptr;
}
bool jsx_active(void* owner) { return g_s && g_s->js && g_s->owner == owner; }

bool jsx_pump(void* owner, uint32_t now, std::string* outHtml) {
#ifndef PSB_NO_JS
    Session* s = g_s;
    if (!s || !s->js || s->owner != owner) return false;
    const uint32_t start = hnow();
    int ran = 0;
    // temporizadores vencidos, por orden de vencimiento
    while (ran < 40 && hnow() - start < 14) {
        int best = -1;
        for (size_t i = 0; i < s->timers.size(); i++) {
            const Timer& t = s->timers[i];
            if (t.id < 0) continue;
            if (t.due <= now && (best < 0 || t.due < s->timers[static_cast<size_t>(best)].due)) best = static_cast<int>(i);
        }
        if (best < 0) break;
        Timer t = s->timers[static_cast<size_t>(best)];
        if (t.interval >= 0) {
            Timer& slot = s->timers[static_cast<size_t>(best)];
            slot.due = std::max(now, t.due + static_cast<uint32_t>(t.interval));
            if (slot.due <= now) slot.due = now + static_cast<uint32_t>(t.interval);
            t.fn = JS_DupValue(s->ctx, t.fn);
        } else {
            s->timers[static_cast<size_t>(best)].id = -1;  // una sola vez: el valor pasa a 't'
            s->timers[static_cast<size_t>(best)].fn = JS_UNDEFINED;
        }
        s->deadline = hnow() + 250;
        JSValue r = JS_Call(s->ctx, t.fn, JS_UNDEFINED, 0, nullptr);
        if (JS_IsException(r)) {
            if (s->timerErrLogged < 12) { logException(s->ctx, "temporizador"); s->timerErrLogged++; }
            else { JSValue ex = JS_GetException(s->ctx); JS_FreeValue(s->ctx, ex); if (s->timerErrLogged++ == 12) hlog("[js] (se omiten los siguientes errores de temporizador)"); }
            if (t.interval >= 0) {  // un setInterval que falla siempre se cancela a los 3 errores seguidos
                for (Timer& x : s->timers) if (x.id == t.id) {
                    if (++x.errors >= 3) { JS_FreeValue(s->ctx, x.fn); x.fn = JS_UNDEFINED; x.id = -1; hlog("[js] intervalo cancelado tras 3 errores seguidos"); }
                    break;
                }
            }
        } else {
            JS_FreeValue(s->ctx, r);
            if (t.interval >= 0) for (Timer& x : s->timers) if (x.id == t.id) { x.errors = 0; break; }
        }
        JS_FreeValue(s->ctx, t.fn);
        drainJobs(s);
        ran++;
    }
    // compactar la lista de vencidos
    s->timers.erase(std::remove_if(s->timers.begin(), s->timers.end(), [](const Timer& t) { return t.id < 0; }), s->timers.end());
    if (s->dom.dirty && now - s->lastSer >= 120) {
        *outHtml = s->dom.serialize(true, true);
        s->lastSer = now;
        return true;
    }
#else
    (void)owner; (void)now; (void)outHtml;
#endif
    return false;
}

std::string jsx_status() { return g_s ? g_s->status : std::string("(sin sesion)"); }

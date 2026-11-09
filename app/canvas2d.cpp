// Canvas 2D por software (v0.9.5). Ver canvas2d.h.
#include "canvas2d.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include "../third_party/font8x8_basic.h"

namespace {

const double kPi = 3.14159265358979323846;
struct Pt { double x, y; };
struct Mat {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    Pt ap(double x, double y) const { return {a * x + c * y + e, b * x + d * y + f}; }
    Mat mul(const Mat& m) const {  // this * m (aplica m primero)
        Mat r;
        r.a = a * m.a + c * m.b; r.b = b * m.a + d * m.b;
        r.c = a * m.c + c * m.d; r.d = b * m.c + d * m.d;
        r.e = a * m.e + c * m.f + e; r.f = b * m.e + d * m.f + f;
        return r;
    }
    double det() const { return a * d - b * c; }
    bool inv(Mat& o) const {
        const double dt = det();
        if (std::fabs(dt) < 1e-12) return false;
        o.a = d / dt; o.b = -b / dt; o.c = -c / dt; o.d = a / dt;
        o.e = (c * f - d * e) / dt; o.f = (b * e - a * f) / dt;
        return true;
    }
    double scale() const { return std::sqrt(std::fabs(det())); }
};
struct SubPath { std::vector<Pt> p; bool closed = false; };
struct Grad {
    bool radial = false;
    double x0 = 0, y0 = 0, r0 = 0, x1 = 0, y1 = 0, r1 = 0;
    std::vector<std::pair<double, uint32_t>> stops;
};
struct State {
    Mat m;
    uint32_t fill = 0xFF000000u, stroke = 0xFF000000u;
    int fillGrad = -1, strokeGrad = -1;
    double lineWidth = 1, alpha = 1;
    int comp = 0, cap = 0, join = 0;
    bool smooth = true;
    int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
    double fontPx = 10;
    int align = 0;     // 0 start/left, 1 center, 2 right/end
    int baseline = 0;  // 0 alphabetic, 1 top, 2 middle, 3 bottom
};
struct Src { uint32_t color; const Grad* g; Mat gm; };  // color solido o degradado (con la matriz del momento de pintar)

struct Cv {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    State st;
    std::vector<State> stack;
    std::vector<SubPath> path;
    std::vector<Grad> grads;
    std::string text;
    uint32_t stopColor = 0xFF000000u;
    bool dirty = true, used = false;
    std::vector<float> cov;  // cobertura por fila (reutilizada)

    Cv(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_, 0u) { st.cx0 = 0; st.cy0 = 0; st.cx1 = w_; st.cy1 = h_; cov.assign(static_cast<size_t>(w_) + 2, 0.f); }
    void resize(int w_, int h_) {
        w = w_; h = h_; px.assign(static_cast<size_t>(w) * h, 0u);
        st = State(); st.cx0 = 0; st.cy0 = 0; st.cx1 = w; st.cy1 = h; stack.clear(); path.clear();
        cov.assign(static_cast<size_t>(w) + 2, 0.f); dirty = true;
    }

    // ------------------------------------------------------------ mezcla
    void blendPx(uint32_t& d, uint32_t s, int sa) const {
        if (sa <= 0) return;
        if (sa > 255) sa = 255;
        const int da = static_cast<int>(d >> 24);
        if (st.comp == 2) {  // destination-out
            const int na = da * (255 - sa) / 255;
            d = na == 0 ? 0u : ((static_cast<uint32_t>(na) << 24) | (d & 0xFFFFFFu));
            return;
        }
        if (st.comp == 1) {  // lighter (suma)
            const int oa = std::min(255, da + sa);
            if (oa == 0) return;
            int c[3];
            for (int k = 0; k < 3; k++) {
                const int sh = 16 - 8 * k;
                const int pd = static_cast<int>((d >> sh) & 255) * da / 255, ps = static_cast<int>((s >> sh) & 255) * sa / 255;
                c[k] = std::min(255, (pd + ps) * 255 / oa);
            }
            d = (static_cast<uint32_t>(oa) << 24) | (static_cast<uint32_t>(c[0]) << 16) | (static_cast<uint32_t>(c[1]) << 8) | static_cast<uint32_t>(c[2]);
            return;
        }
        if (sa == 255) { d = 0xFF000000u | (s & 0xFFFFFFu); return; }
        if (da == 0) { d = (static_cast<uint32_t>(sa) << 24) | (s & 0xFFFFFFu); return; }
        const int oa = sa + da * (255 - sa) / 255;
        if (oa <= 0) return;
        int c[3];
        for (int k = 0; k < 3; k++) {
            const int sh = 16 - 8 * k;
            const int sc = static_cast<int>((s >> sh) & 255), dc = static_cast<int>((d >> sh) & 255);
            c[k] = (sc * sa * 255 + dc * da * (255 - sa)) / (oa * 255);
            if (c[k] > 255) c[k] = 255;
        }
        d = (static_cast<uint32_t>(oa) << 24) | (static_cast<uint32_t>(c[0]) << 16) | (static_cast<uint32_t>(c[1]) << 8) | static_cast<uint32_t>(c[2]);
    }
    static uint32_t gradColor(const Grad& g, double t) {
        if (g.stops.empty()) return 0;
        if (t <= g.stops.front().first) return g.stops.front().second;
        if (t >= g.stops.back().first) return g.stops.back().second;
        for (size_t i = 1; i < g.stops.size(); i++) {
            if (t <= g.stops[i].first) {
                const double t0 = g.stops[i - 1].first, t1 = g.stops[i].first;
                const double u = t1 > t0 ? (t - t0) / (t1 - t0) : 1.0;
                uint32_t out = 0;
                for (int k = 0; k < 4; k++) {
                    const int sh = 24 - 8 * k;
                    const double a = (g.stops[i - 1].second >> sh) & 255, b = (g.stops[i].second >> sh) & 255;
                    out |= static_cast<uint32_t>(std::lround(a + (b - a) * u)) << sh;
                }
                return out;
            }
        }
        return g.stops.back().second;
    }
    uint32_t srcAt(const Src& s, int x, int y) const {
        if (!s.g) return s.color;
        const Grad& g = *s.g;
        const Pt p0 = s.gm.ap(g.x0, g.y0), p1 = s.gm.ap(g.x1, g.y1);
        double t;
        if (!g.radial) {
            const double dx = p1.x - p0.x, dy = p1.y - p0.y, l2 = dx * dx + dy * dy;
            t = l2 > 0 ? ((x + 0.5 - p0.x) * dx + (y + 0.5 - p0.y) * dy) / l2 : 0;
        } else {  // aproximacion: distancia al centro final, de r0 a r1 (escalada por la matriz)
            const double sc = s.gm.scale(), r0 = g.r0 * sc, r1 = g.r1 * sc;
            const double d = std::hypot(x + 0.5 - p1.x, y + 0.5 - p1.y);
            t = r1 > r0 ? (d - r0) / (r1 - r0) : 0;
        }
        return gradColor(g, t);
    }
    int effA(uint32_t c, double cover) const { return static_cast<int>(std::lround(static_cast<double>(c >> 24) * st.alpha * cover)); }

    // ------------------------------------------------------------ relleno de poligonos (scanline, 4 subfilas, cobertura horizontal exacta)
    struct Edge { double ymin, ymax, x0, slope; int dir; };
    void fillPolys(const std::vector<std::vector<Pt>>& polys, bool evenodd, const Src& src) {
        double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
        std::vector<Edge> edges;
        for (const auto& pl : polys) {
            const size_t n = pl.size();
            if (n < 2) continue;
            for (size_t i = 0; i < n; i++) {
                const Pt a = pl[i], b = pl[(i + 1) % n];
                if (!std::isfinite(a.x) || !std::isfinite(a.y)) return;
                minx = std::min(minx, a.x); maxx = std::max(maxx, a.x); miny = std::min(miny, a.y); maxy = std::max(maxy, a.y);
                if (a.y == b.y) continue;
                Edge e;
                if (a.y < b.y) { e.ymin = a.y; e.ymax = b.y; e.x0 = a.x; e.slope = (b.x - a.x) / (b.y - a.y); e.dir = 1; }
                else { e.ymin = b.y; e.ymax = a.y; e.x0 = b.x; e.slope = (a.x - b.x) / (a.y - b.y); e.dir = -1; }
                edges.push_back(e);
            }
        }
        if (edges.empty()) return;
        const int y0 = std::max(st.cy0, static_cast<int>(std::floor(miny))), y1 = std::min(st.cy1, static_cast<int>(std::ceil(maxy)));
        const int xa = std::max(st.cx0, static_cast<int>(std::floor(minx))), xb = std::min(st.cx1, static_cast<int>(std::ceil(maxx)));
        if (y0 >= y1 || xa >= xb) return;
        std::sort(edges.begin(), edges.end(), [](const Edge& p, const Edge& q) { return p.ymin < q.ymin; });
        std::vector<int> active;
        size_t nextEdge = 0;
        std::vector<std::pair<double, int>> cr;
        const int SUB = 4;
        for (int y = y0; y < y1; y++) {
            bool any = false;
            for (int k = 0; k < SUB; k++) {
                const double ys = y + (k + 0.5) / SUB;
                while (nextEdge < edges.size() && edges[nextEdge].ymin <= ys) active.push_back(static_cast<int>(nextEdge++));
                cr.clear();
                for (size_t i = 0; i < active.size();) {
                    const Edge& e = edges[active[i]];
                    if (e.ymax <= ys) { active[i] = active.back(); active.pop_back(); continue; }
                    cr.push_back({e.x0 + (ys - e.ymin) * e.slope, e.dir});
                    i++;
                }
                if (cr.size() < 2) continue;
                std::sort(cr.begin(), cr.end());
                int wind = 0;
                for (size_t i = 0; i + 1 < cr.size(); i++) {
                    wind += cr[i].second;
                    const bool inside = evenodd ? ((i & 1) == 0) : (wind != 0);
                    if (!inside) continue;
                    double sa = std::max<double>(cr[i].first, xa), sb = std::min<double>(cr[i + 1].first, xb);
                    if (sb <= sa) continue;
                    const int ia = static_cast<int>(sa), ib = static_cast<int>(sb);
                    const float q = 1.0f / SUB;
                    if (ia == ib) cov[ia] += static_cast<float>(sb - sa) * q;
                    else {
                        cov[ia] += static_cast<float>(ia + 1 - sa) * q;
                        for (int x = ia + 1; x < ib; x++) cov[x] += q;
                        if (ib < w + 1) cov[ib] += static_cast<float>(sb - ib) * q;
                    }
                    any = true;
                }
            }
            if (any) {
                uint32_t* row = &px[static_cast<size_t>(y) * w];
                for (int x = xa; x <= xb && x < w; x++) {
                    float cv = cov[x];
                    if (cv <= 0.f) continue;
                    cov[x] = 0.f;
                    if (cv > 1.f) cv = 1.f;
                    const uint32_t c = srcAt(src, x, y);
                    blendPx(row[x], c, effA(c, cv));
                }
                for (int x = xa; x <= xb + 1 && x < static_cast<int>(cov.size()); x++) cov[x] = 0.f;
            }
        }
        used = true; dirty = true;
    }

    // ------------------------------------------------------------ rutas
    void ensureSub(const Pt& start) {
        if (path.empty() || path.back().closed) {
            SubPath sp; sp.p.push_back(start); path.push_back(sp);
        }
    }
    void moveTo(double x, double y) { SubPath sp; sp.p.push_back(st.m.ap(x, y)); path.push_back(sp); }
    void lineTo(double x, double y) {
        const Pt p = st.m.ap(x, y);
        if (path.empty()) { SubPath sp; sp.p.push_back(p); path.push_back(sp); return; }
        if (path.back().closed) { SubPath sp; sp.p.push_back(path.back().p.front()); path.push_back(sp); }
        path.back().p.push_back(p);
    }
    void closePath() { if (!path.empty() && !path.back().closed) path.back().closed = true; }
    Pt curPt() const { return path.empty() || path.back().p.empty() ? Pt{0, 0} : (path.back().closed ? path.back().p.front() : path.back().p.back()); }
    void quadTo(double cx, double cy, double x, double y) {
        const Pt c = st.m.ap(cx, cy), e = st.m.ap(x, y);
        if (path.empty()) { SubPath sp; sp.p.push_back(c); path.push_back(sp); }
        const Pt s = curPt();
        ensureSub(s);
        const int n = 16;
        for (int i = 1; i <= n; i++) {
            const double t = static_cast<double>(i) / n, u = 1 - t;
            path.back().p.push_back({u * u * s.x + 2 * u * t * c.x + t * t * e.x, u * u * s.y + 2 * u * t * c.y + t * t * e.y});
        }
    }
    void bezierTo(double x1, double y1, double x2, double y2, double x, double y) {
        const Pt c1 = st.m.ap(x1, y1), c2 = st.m.ap(x2, y2), e = st.m.ap(x, y);
        if (path.empty()) { SubPath sp; sp.p.push_back(c1); path.push_back(sp); }
        const Pt s = curPt();
        ensureSub(s);
        const int n = 24;
        for (int i = 1; i <= n; i++) {
            const double t = static_cast<double>(i) / n, u = 1 - t;
            const double b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
            path.back().p.push_back({b0 * s.x + b1 * c1.x + b2 * c2.x + b3 * e.x, b0 * s.y + b1 * c1.y + b2 * c2.y + b3 * e.y});
        }
    }
    void ellipse(double x, double y, double rx, double ry, double rot, double a0, double a1, bool ccw) {
        if (rx < 0 || ry < 0) return;
        double da = a1 - a0;
        if (!ccw) { if (da >= 2 * kPi) da = 2 * kPi; else { da = std::fmod(da, 2 * kPi); if (da < 0) da += 2 * kPi; } }
        else { if (da <= -2 * kPi) da = -2 * kPi; else { da = std::fmod(da, 2 * kPi); if (da > 0) da -= 2 * kPi; } }
        const double rd = std::max(rx, ry) * st.m.scale();
        int n = static_cast<int>(std::fabs(da) * std::sqrt(std::max(1.0, rd)) * 1.6) + 6;
        n = std::max(8, std::min(256, n));
        const double cr = std::cos(rot), sr = std::sin(rot);
        for (int i = 0; i <= n; i++) {
            const double t = a0 + da * i / n;
            const double ex = rx * std::cos(t), ey = ry * std::sin(t);
            const double ux = x + ex * cr - ey * sr, uy = y + ex * sr + ey * cr;
            if (i == 0) { if (path.empty() || path.back().closed || path.back().p.empty()) moveTo(ux, uy); else lineTo(ux, uy); }
            else lineTo(ux, uy);
        }
    }
    void arcTo(double x1, double y1, double x2, double y2, double r) {  // aproximacion: recta hasta la esquina con radio r
        const Pt s = curPt();
        Mat im;
        if (!st.m.inv(im)) return;
        const Pt p0 = im.ap(s.x, s.y);
        const double v1x = p0.x - x1, v1y = p0.y - y1, v2x = x2 - x1, v2y = y2 - y1;
        const double l1 = std::hypot(v1x, v1y), l2 = std::hypot(v2x, v2y);
        if (path.empty() || l1 == 0 || l2 == 0 || r <= 0) { lineTo(x1, y1); return; }
        const double ux = v1x / l1, uy = v1y / l1, wx = v2x / l2, wy = v2y / l2;
        const double cosA = ux * wx + uy * wy;
        const double ang = std::acos(std::max(-1.0, std::min(1.0, cosA)));
        if (std::fabs(std::sin(ang)) < 1e-6) { lineTo(x1, y1); return; }
        const double tl = r / std::tan(ang / 2);
        const double ax = x1 + ux * tl, ay = y1 + uy * tl, bx = x1 + wx * tl, by = y1 + wy * tl;
        const double cross = ux * wy - uy * wx;
        const double bis = (ux + wx), bisy = (uy + wy), bl = std::hypot(bis, bisy);
        if (bl == 0) { lineTo(x1, y1); return; }
        const double cd = r / std::sin(ang / 2);
        const double ccx = x1 + bis / bl * cd, ccy = y1 + bisy / bl * cd;
        double a0 = std::atan2(ay - ccy, ax - ccx), a1 = std::atan2(by - ccy, bx - ccx);
        lineTo(ax, ay);
        ellipse(ccx, ccy, r, r, 0, a0, a1, cross > 0);
        lineTo(bx, by);
    }
    std::vector<std::vector<Pt>> pathPolys() const {
        std::vector<std::vector<Pt>> v;
        for (const SubPath& sp : path) if (sp.p.size() >= 2) v.push_back(sp.p);
        return v;
    }
    static void orient(std::vector<Pt>& p) {  // area positiva (para que la union por "nonzero" no se cancele)
        double a = 0;
        for (size_t i = 0; i < p.size(); i++) { const Pt& u = p[i]; const Pt& v = p[(i + 1) % p.size()]; a += u.x * v.y - v.x * u.y; }
        if (a < 0) std::reverse(p.begin(), p.end());
    }
    static void circlePoly(std::vector<std::vector<Pt>>& out, Pt c, double r) {
        const int n = std::max(8, std::min(32, static_cast<int>(r * 3)));
        std::vector<Pt> p;
        for (int i = 0; i < n; i++) { const double t = 2 * kPi * i / n; p.push_back({c.x + r * std::cos(t), c.y + r * std::sin(t)}); }
        orient(p); out.push_back(p);
    }
    std::vector<std::vector<Pt>> strokePolys(const std::vector<SubPath>& sps, double lw) const {
        std::vector<std::vector<Pt>> out;
        const double hw = std::max(0.5, lw * st.m.scale() * 0.5);
        for (const SubPath& sp : sps) {
            std::vector<Pt> pts;
            for (const Pt& q : sp.p) if (pts.empty() || std::hypot(q.x - pts.back().x, q.y - pts.back().y) > 1e-9) pts.push_back(q);
            if (sp.closed && pts.size() > 1 && std::hypot(pts.front().x - pts.back().x, pts.front().y - pts.back().y) > 1e-9) pts.push_back(pts.front());
            if (pts.size() == 1) { if (st.cap == 1 || sp.closed) circlePoly(out, pts[0], hw); continue; }
            for (size_t i = 0; i + 1 < pts.size(); i++) {
                Pt a = pts[i], b = pts[i + 1];
                const double dx = b.x - a.x, dy = b.y - a.y, l = std::hypot(dx, dy);
                if (l < 1e-9) continue;
                const double nx = -dy / l * hw, ny = dx / l * hw;
                if (st.cap == 2 && !sp.closed) {
                    if (i == 0) { a.x -= dx / l * hw; a.y -= dy / l * hw; }
                    if (i + 2 == pts.size()) { b.x += dx / l * hw; b.y += dy / l * hw; }
                }
                std::vector<Pt> q = {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}};
                orient(q); out.push_back(q);
            }
            if (hw >= 1.0) {  // uniones redondas en los vertices interiores (y extremos si cap = round)
                for (size_t i = 1; i + 1 < pts.size(); i++) circlePoly(out, pts[i], hw);
                if (st.cap == 1 && !sp.closed) { circlePoly(out, pts.front(), hw); circlePoly(out, pts.back(), hw); }
            }
        }
        return out;
    }
    Src fillSrc() { Src s; s.color = st.fill; s.g = st.fillGrad >= 0 && st.fillGrad < static_cast<int>(grads.size()) ? &grads[st.fillGrad] : nullptr; s.gm = st.m; return s; }
    Src strokeSrc() { Src s; s.color = st.stroke; s.g = st.strokeGrad >= 0 && st.strokeGrad < static_cast<int>(grads.size()) ? &grads[st.strokeGrad] : nullptr; s.gm = st.m; return s; }
    std::vector<Pt> rectPoly(double x, double y, double rw, double rh) const {
        return {st.m.ap(x, y), st.m.ap(x + rw, y), st.m.ap(x + rw, y + rh), st.m.ap(x, y + rh)};
    }

    // ------------------------------------------------------------ rect rapido (sin rotacion)
    bool axisAligned() const { return std::fabs(st.m.b) < 1e-12 && std::fabs(st.m.c) < 1e-12; }
    void fillRect(double x, double y, double rw, double rh, const Src& src) {
        if (!axisAligned()) { fillPolys({rectPoly(x, y, rw, rh)}, false, src); return; }
        const Pt p = st.m.ap(x, y), q = st.m.ap(x + rw, y + rh);
        const double fx0 = std::min(p.x, q.x), fx1 = std::max(p.x, q.x), fy0 = std::min(p.y, q.y), fy1 = std::max(p.y, q.y);
        const int ix0 = std::max(st.cx0, static_cast<int>(std::floor(fx0))), ix1 = std::min(st.cx1, static_cast<int>(std::ceil(fx1)));
        const int iy0 = std::max(st.cy0, static_cast<int>(std::floor(fy0))), iy1 = std::min(st.cy1, static_cast<int>(std::ceil(fy1)));
        // tramo central con cobertura completa en x: si el color es solido, se calcula una vez (y si es opaco, se rellena de golpe)
        const int mx0 = std::max(ix0, static_cast<int>(std::ceil(fx0))), mx1 = std::min(ix1, static_cast<int>(std::floor(fx1)));
        const bool solid = !src.g;
        const int sa = solid ? effA(src.color, 1.0) : 0;
        const bool opaque = solid && st.comp == 0 && sa >= 255;
        const uint32_t oc = 0xFF000000u | (src.color & 0xFFFFFFu);
        for (int j = iy0; j < iy1; j++) {
            const double cy = std::min(fy1, j + 1.0) - std::max(fy0, static_cast<double>(j));
            if (cy <= 0) continue;
            uint32_t* row = &px[static_cast<size_t>(j) * w];
            const bool fullRow = cy >= 0.999999;
            for (int i = ix0; i < ix1;) {
                if (fullRow && i == mx0 && mx1 > mx0) {  // tramo central
                    if (opaque) std::fill(row + mx0, row + mx1, oc);
                    else if (solid) for (int x = mx0; x < mx1; x++) blendPx(row[x], src.color, sa);
                    else for (int x = mx0; x < mx1; x++) { const uint32_t c = srcAt(src, x, j); blendPx(row[x], c, effA(c, 1.0)); }
                    i = mx1;
                    continue;
                }
                const double cx = std::min(fx1, i + 1.0) - std::max(fx0, static_cast<double>(i));
                if (cx > 0) {
                    const uint32_t c = srcAt(src, i, j);
                    blendPx(row[i], c, effA(c, cx * cy));
                }
                i++;
            }
        }
        used = true; dirty = true;
    }
    void clearRect(double x, double y, double rw, double rh) {
        if (!axisAligned()) { const int sc = st.comp; st.comp = 2; Src s{0xFFFFFFFFu, nullptr, st.m}; const double al = st.alpha; st.alpha = 1; fillPolys({rectPoly(x, y, rw, rh)}, false, s); st.alpha = al; st.comp = sc; return; }
        const Pt p = st.m.ap(x, y), q = st.m.ap(x + rw, y + rh);
        const int ix0 = std::max(st.cx0, static_cast<int>(std::lround(std::min(p.x, q.x)))), ix1 = std::min(st.cx1, static_cast<int>(std::lround(std::max(p.x, q.x))));
        const int iy0 = std::max(st.cy0, static_cast<int>(std::lround(std::min(p.y, q.y)))), iy1 = std::min(st.cy1, static_cast<int>(std::lround(std::max(p.y, q.y))));
        for (int j = iy0; j < iy1; j++) std::fill(px.begin() + static_cast<size_t>(j) * w + ix0, px.begin() + static_cast<size_t>(j) * w + std::max(ix0, ix1), 0u);
        dirty = true;
    }

    // ------------------------------------------------------------ imagenes
    void drawImage(const Cv& s, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh) {
        if (s.w <= 0 || s.h <= 0 || dw == 0 || dh == 0 || sw == 0 || sh == 0) return;
        if (sw < 0) { sx += sw; sw = -sw; }
        if (sh < 0) { sy += sh; sh = -sh; }
        if (dw < 0) { dx += dw; dw = -dw; sx += sw; sw = -sw; }  // espejo horizontal
        if (dh < 0) { dy += dh; dh = -dh; sy += sh; sh = -sh; }
        Mat im;
        if (!st.m.inv(im)) return;
        const auto corners = rectPoly(dx, dy, dw, dh);
        double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
        for (const Pt& c : corners) { minx = std::min(minx, c.x); maxx = std::max(maxx, c.x); miny = std::min(miny, c.y); maxy = std::max(maxy, c.y); }
        const int x0 = std::max(st.cx0, static_cast<int>(std::floor(minx))), x1 = std::min(st.cx1, static_cast<int>(std::ceil(maxx)));
        const int y0 = std::max(st.cy0, static_cast<int>(std::floor(miny))), y1 = std::min(st.cy1, static_cast<int>(std::ceil(maxy)));
        const bool simple = std::fabs(st.m.a - 1) < 1e-9 && std::fabs(st.m.d - 1) < 1e-9 && st.m.b == 0 && st.m.c == 0 && std::fabs(dw - std::fabs(sw)) < 1e-9 && std::fabs(dh - std::fabs(sh)) < 1e-9;
        if (simple && sw > 0 && sh > 0) {  // camino rapido: traslacion pura (sprites): sin transformacion inversa por pixel
            const int xa = std::max(x0, static_cast<int>(std::ceil(dx + st.m.e - 0.5))), xb = std::min(x1, static_cast<int>(std::ceil(dx + dw + st.m.e - 0.5)));
            const int ya = std::max(y0, static_cast<int>(std::ceil(dy + st.m.f - 0.5))), yb = std::min(y1, static_cast<int>(std::ceil(dy + dh + st.m.f - 0.5)));
            const int ox = static_cast<int>(std::floor(0.5 - st.m.e + sx - dx)), oy = static_cast<int>(std::floor(0.5 - st.m.f + sy - dy));
            const int sxl = std::max(0, static_cast<int>(std::floor(sx))), sxh = std::min(s.w, static_cast<int>(std::ceil(sx + sw)));
            const int syl = std::max(0, static_cast<int>(std::floor(sy))), syh = std::min(s.h, static_cast<int>(std::ceil(sy + sh)));
            const bool direct = st.comp == 0 && st.alpha >= 0.999999;
            for (int y = ya; y < yb; y++) {
                const int syi = y + oy;
                if (syi < syl || syi >= syh) continue;
                uint32_t* row = &px[static_cast<size_t>(y) * w];
                const uint32_t* srow = &s.px[static_cast<size_t>(syi) * s.w];
                for (int x = xa; x < xb; x++) {
                    const int sxi = x + ox;
                    if (sxi < sxl || sxi >= sxh) continue;
                    const uint32_t c = srow[sxi];
                    const uint32_t a = c >> 24;
                    if (direct && a == 255) row[x] = c;
                    else if (a) blendPx(row[x], c, effA(c, 1.0));
                }
            }
            used = true; dirty = true;
            return;
        }
        const bool bil = st.smooth && !simple;
        const double ssx = sw / dw, ssy = sh / dh;
        const double lox = std::min(sx, sx + sw), hix = std::max(sx, sx + sw), loy = std::min(sy, sy + sh), hiy = std::max(sy, sy + sh);
        for (int y = y0; y < y1; y++) {
            uint32_t* row = &px[static_cast<size_t>(y) * w];
            for (int x = x0; x < x1; x++) {
                const Pt u = im.ap(x + 0.5, y + 0.5);
                if (u.x < dx || u.x >= dx + dw || u.y < dy || u.y >= dy + dh) continue;
                const double fx = sx + (u.x - dx) * ssx, fy = sy + (u.y - dy) * ssy;
                uint32_t c;
                if (!bil) {
                    const int ix = std::max(static_cast<int>(lox), std::min(static_cast<int>(hix) - 1 < 0 ? 0 : static_cast<int>(std::ceil(hix)) - 1, static_cast<int>(std::floor(fx))));
                    const int iy = std::max(static_cast<int>(loy), std::min(static_cast<int>(std::ceil(hiy)) - 1, static_cast<int>(std::floor(fy))));
                    if (ix < 0 || iy < 0 || ix >= s.w || iy >= s.h) continue;
                    c = s.px[static_cast<size_t>(iy) * s.w + ix];
                } else {
                    double gx = std::max(lox, std::min(hix - 1e-6, fx)) - 0.5, gy = std::max(loy, std::min(hiy - 1e-6, fy)) - 0.5;
                    int ix = static_cast<int>(std::floor(gx)), iy = static_cast<int>(std::floor(gy));
                    const double tx = gx - ix, ty = gy - iy;
                    auto at = [&](int xx, int yy) { xx = std::max(0, std::min(s.w - 1, xx)); yy = std::max(0, std::min(s.h - 1, yy)); return s.px[static_cast<size_t>(yy) * s.w + xx]; };
                    const uint32_t c00 = at(ix, iy), c10 = at(ix + 1, iy), c01 = at(ix, iy + 1), c11 = at(ix + 1, iy + 1);
                    c = 0;
                    // interpolacion con alfa premultiplicado para no ensuciar los bordes
                    double acc[4] = {0, 0, 0, 0};
                    const uint32_t cs[4] = {c00, c10, c01, c11};
                    const double wt[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
                    for (int k = 0; k < 4; k++) {
                        const double a = (cs[k] >> 24) & 255;
                        acc[0] += wt[k] * a; acc[1] += wt[k] * a * ((cs[k] >> 16) & 255); acc[2] += wt[k] * a * ((cs[k] >> 8) & 255); acc[3] += wt[k] * a * (cs[k] & 255);
                    }
                    if (acc[0] > 0.5) {
                        const uint32_t a = static_cast<uint32_t>(std::lround(acc[0]));
                        auto ch = [&](double v) { return static_cast<uint32_t>(std::lround(std::max(0.0, std::min(255.0, v / acc[0])))); };
                        c = (a << 24) | (ch(acc[1]) << 16) | (ch(acc[2]) << 8) | ch(acc[3]);
                    }
                }
                blendPx(row[x], c, effA(c, 1.0));
            }
        }
        used = true; dirty = true;
    }

    // ------------------------------------------------------------ texto (fuente 8x8 escalada)
    double textWidth(const std::string& t) const { return 8.0 * (st.fontPx / 8.0) * static_cast<double>(t.size()); }
    void drawText(double x, double y, double maxw, bool strokeOnly, const Src& src) {
        const double scale = st.fontPx / 8.0;
        double wdt = textWidth(text);
        double sx = 1.0;
        if (maxw > 0 && wdt > maxw) sx = maxw / wdt;
        double ox = x;
        if (st.align == 1) ox -= wdt * sx / 2; else if (st.align == 2) ox -= wdt * sx;
        double oy = y;
        switch (st.baseline) { case 1: break; case 2: oy -= st.fontPx / 2; break; case 3: oy -= st.fontPx; break; default: oy -= st.fontPx * 7.0 / 8.0; break; }
        (void)strokeOnly;
        for (size_t ci = 0; ci < text.size(); ci++) {
            const unsigned char ch = static_cast<unsigned char>(text[ci]);
            if (ch >= 128) continue;
            for (int row = 0; row < 8; row++) {
                const unsigned char bits = font8x8_basic[ch][row];
                if (!bits) continue;
                int col = 0;
                while (col < 8) {
                    if (!(bits & (1 << col))) { col++; continue; }
                    int e = col;
                    while (e < 8 && (bits & (1 << e))) e++;  // tramo horizontal continuo
                    const double gx0 = ox + (ci * 8.0 + col) * scale * sx, gx1 = ox + (ci * 8.0 + e) * scale * sx;
                    fillRectNoSnap(gx0, oy + row * scale, gx1 - gx0, scale, src);
                    col = e;
                }
            }
        }
    }
    void fillRectNoSnap(double x, double y, double rw, double rh, const Src& src) {
        if (!axisAligned()) { fillPolys({rectPoly(x, y, rw, rh)}, false, src); return; }
        fillRect(x, y, rw, rh, src);
    }
};

std::vector<std::unique_ptr<Cv>> g_cvs;
Cv* cvOf(int id) { return id >= 1 && id <= static_cast<int>(g_cvs.size()) ? g_cvs[id - 1].get() : nullptr; }

bool hexv(char c, int& v) { if (c >= '0' && c <= '9') v = c - '0'; else if (c >= 'a' && c <= 'f') v = c - 'a' + 10; else if (c >= 'A' && c <= 'F') v = c - 'A' + 10; else return false; return true; }
uint32_t packA(int a, int r, int g, int b) { return (static_cast<uint32_t>(std::max(0, std::min(255, a))) << 24) | (static_cast<uint32_t>(std::max(0, std::min(255, r))) << 16) | (static_cast<uint32_t>(std::max(0, std::min(255, g))) << 8) | static_cast<uint32_t>(std::max(0, std::min(255, b))); }
double hue2rgb(double p, double q, double t) { if (t < 0) t += 1; if (t > 1) t -= 1; if (t < 1.0 / 6) return p + (q - p) * 6 * t; if (t < 0.5) return q; if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6; return p; }
std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c))); return s; }
std::vector<double> nums(const std::string& s, size_t from) {
    std::vector<double> v;
    size_t i = from;
    while (i < s.size()) {
        while (i < s.size() && !(isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == '-' || s[i] == '+')) i++;
        if (i >= s.size()) break;
        char* end = nullptr;
        const double d = strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) { i++; continue; }
        const size_t e = static_cast<size_t>(end - s.c_str());
        v.push_back(e < s.size() && s[e] == '%' ? -1000000.0 - d : d);  // porcentaje codificado como -1e6 - valor
        i = e + 1;
    }
    return v;
}
double pct(double v) { return v <= -1000000.0 ? -(v + 1000000.0) : -1; }

}  // namespace

bool c2d_parse_color(const std::string& in, uint32_t& out) {
    std::string s;
    for (char c : in) if (!isspace(static_cast<unsigned char>(c)) || (c == ' ')) s += c;
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (s.empty()) return false;
    if (s[0] == '#') {
        int v[8];
        const size_t n = s.size() - 1;
        if (n != 3 && n != 4 && n != 6 && n != 8) return false;
        for (size_t i = 0; i < n; i++) if (!hexv(s[i + 1], v[i])) return false;
        if (n <= 4) out = packA(n == 4 ? v[3] * 17 : 255, v[0] * 17, v[1] * 17, v[2] * 17);
        else out = packA(n == 8 ? v[6] * 16 + v[7] : 255, v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5]);
        return true;
    }
    const std::string l = lower(s);
    if (l.compare(0, 3, "rgb") == 0 || l.compare(0, 3, "hsl") == 0) {
        const size_t p = l.find('(');
        if (p == std::string::npos) return false;
        const std::vector<double> v = nums(l, p + 1);
        if (v.size() < 3) return false;
        const double al = v.size() >= 4 ? (pct(v[3]) >= 0 ? pct(v[3]) / 100.0 : v[3]) : 1.0;
        if (l.compare(0, 3, "rgb") == 0) {
            auto ch = [&](double x) { return pct(x) >= 0 ? pct(x) * 2.55 : x; };
            out = packA(static_cast<int>(std::lround(al * 255)), static_cast<int>(std::lround(ch(v[0]))), static_cast<int>(std::lround(ch(v[1]))), static_cast<int>(std::lround(ch(v[2]))));
        } else {
            double hh = std::fmod(pct(v[0]) >= 0 ? pct(v[0]) : v[0], 360.0); if (hh < 0) hh += 360; hh /= 360.0;
            const double sat = std::max(0.0, std::min(1.0, (pct(v[1]) >= 0 ? pct(v[1]) : v[1]) / 100.0)), li = std::max(0.0, std::min(1.0, (pct(v[2]) >= 0 ? pct(v[2]) : v[2]) / 100.0));
            double r, g, b;
            if (sat == 0) r = g = b = li;
            else { const double q = li < 0.5 ? li * (1 + sat) : li + sat - li * sat, pp = 2 * li - q; r = hue2rgb(pp, q, hh + 1.0 / 3); g = hue2rgb(pp, q, hh); b = hue2rgb(pp, q, hh - 1.0 / 3); }
            out = packA(static_cast<int>(std::lround(al * 255)), static_cast<int>(std::lround(r * 255)), static_cast<int>(std::lround(g * 255)), static_cast<int>(std::lround(b * 255)));
        }
        return true;
    }
    static const struct { const char* n; uint32_t c; } names[] = {
        {"transparent", 0x00000000u}, {"black", 0xFF000000u}, {"white", 0xFFFFFFFFu}, {"red", 0xFFFF0000u}, {"green", 0xFF008000u}, {"blue", 0xFF0000FFu},
        {"yellow", 0xFFFFFF00u}, {"orange", 0xFFFFA500u}, {"purple", 0xFF800080u}, {"gray", 0xFF808080u}, {"grey", 0xFF808080u}, {"cyan", 0xFF00FFFFu},
        {"aqua", 0xFF00FFFFu}, {"magenta", 0xFFFF00FFu}, {"fuchsia", 0xFFFF00FFu}, {"pink", 0xFFFFC0CBu}, {"brown", 0xFFA52A2Au}, {"lime", 0xFF00FF00u},
        {"navy", 0xFF000080u}, {"teal", 0xFF008080u}, {"silver", 0xFFC0C0C0u}, {"maroon", 0xFF800000u}, {"olive", 0xFF808000u}, {"gold", 0xFFFFD700u},
        {"darkgray", 0xFFA9A9A9u}, {"darkgrey", 0xFFA9A9A9u}, {"lightgray", 0xFFD3D3D3u}, {"lightgrey", 0xFFD3D3D3u}, {"darkred", 0xFF8B0000u},
        {"darkgreen", 0xFF006400u}, {"darkblue", 0xFF00008Bu}, {"lightblue", 0xFFADD8E6u}, {"lightgreen", 0xFF90EE90u}, {"skyblue", 0xFF87CEEBu},
        {"crimson", 0xFFDC143Cu}, {"indigo", 0xFF4B0082u}, {"violet", 0xFFEE82EEu}, {"tan", 0xFFD2B48Cu}, {"coral", 0xFFFF7F50u}, {"salmon", 0xFFFA8072u}};
    for (const auto& n : names) if (l == n.n) { out = n.c; return true; }
    return false;
}

int c2d_create(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > 4096 || h > 4096 || static_cast<long long>(w) * h > 12LL * 1000 * 1000 || g_cvs.size() >= 64) return 0;
    long long total = 0;
    for (const auto& c : g_cvs) total += static_cast<long long>(c->w) * c->h;
    if (total + static_cast<long long>(w) * h > 40LL * 1000 * 1000) return 0;  // ~160 MB de pixeles como maximo entre todos
    g_cvs.emplace_back(new Cv(w, h));
    return static_cast<int>(g_cvs.size());
}
void c2d_resize(int id, int w, int h) {
    Cv* c = cvOf(id);
    if (!c) return;
    w = std::max(1, std::min(4096, w)); h = std::max(1, std::min(4096, h));
    if (static_cast<long long>(w) * h > 12LL * 1000 * 1000) return;
    c->resize(w, h);
}
void c2d_reset_all() { g_cvs.clear(); }
int c2d_add_image(const std::vector<uint32_t>& argb, int w, int h) {
    if (w < 1 || h < 1 || argb.size() < static_cast<size_t>(w) * h) return 0;
    const int id = c2d_create(w, h);
    if (!id) return 0;
    std::copy(argb.begin(), argb.begin() + static_cast<size_t>(w) * h, g_cvs[id - 1]->px.begin());
    return id;
}
bool c2d_info(int id, int& w, int& h, const uint32_t** px) {
    Cv* c = cvOf(id);
    if (!c) return false;
    w = c->w; h = c->h; if (px) *px = c->px.data();
    return true;
}
bool c2d_take_dirty(int id) { Cv* c = cvOf(id); if (!c) return false; const bool d = c->dirty; c->dirty = false; return d; }
int c2d_main() {
    int best = 0; long long ba = -1;
    for (size_t i = 0; i < g_cvs.size(); i++) {
        const Cv& c = *g_cvs[i];
        if (!c.used) continue;
        const long long a = static_cast<long long>(c.w) * c.h;
        if (a >= ba) { ba = a; best = static_cast<int>(i) + 1; }
    }
    return best;
}
int c2d_used_count() { int n = 0; for (const auto& c : g_cvs) if (c->used) n++; return n; }

void c2d_set_str(int id, int what, const std::string& s) {
    Cv* c = cvOf(id);
    if (!c) return;
    uint32_t col;
    switch (what) {
        case C2S_FILL: if (c2d_parse_color(s, col)) { c->st.fill = col; c->st.fillGrad = -1; } break;
        case C2S_STROKE: if (c2d_parse_color(s, col)) { c->st.stroke = col; c->st.strokeGrad = -1; } break;
        case C2S_STOPCOLOR: if (c2d_parse_color(s, col)) c->stopColor = col; break;
        case C2S_FONT: {
            const std::string l = lower(s);
            size_t i = 0; double px = -1;
            while (i < l.size()) {
                if (isdigit(static_cast<unsigned char>(l[i])) || l[i] == '.') {
                    char* end = nullptr; const double v = strtod(l.c_str() + i, &end); const size_t e = static_cast<size_t>(end - l.c_str());
                    if (l.compare(e, 2, "px") == 0) { px = v; break; }
                    if (l.compare(e, 2, "pt") == 0) { px = v * 4.0 / 3.0; break; }
                    if (l.compare(e, 2, "em") == 0) { px = v * 16.0; break; }
                    i = e;
                } else i++;
            }
            if (px > 0) c->st.fontPx = std::min(px, 400.0);
            break;
        }
        case C2S_ALIGN: c->st.align = (s == "center") ? 1 : ((s == "right" || s == "end") ? 2 : 0); break;
        case C2S_BASELINE: c->st.baseline = (s == "top" || s == "hanging") ? 1 : (s == "middle") ? 2 : ((s == "bottom" || s == "ideographic") ? 3 : 0); break;
        case C2S_COMPOSITE: c->st.comp = (s == "lighter") ? 1 : (s == "destination-out") ? 2 : 0; break;
        case C2S_TEXT: c->text = s.size() > 2000 ? s.substr(0, 2000) : s; break;
        default: break;
    }
}

double c2d_op(int id, int op, const double* a, int n) {
    Cv* c = cvOf(id);
    if (!c) return 0;
    auto A = [&](int i) { return i < n ? a[i] : 0.0; };
    for (int i = 0; i < n; i++) if (!std::isfinite(a[i])) return 0;  // spec: argumentos no finitos se ignoran
    switch (op) {
        case C2D_FILLRECT: c->fillRect(A(0), A(1), A(2), A(3), c->fillSrc()); break;
        case C2D_STROKERECT: {
            std::vector<SubPath> sp(1); sp[0].closed = true; sp[0].p = c->rectPoly(A(0), A(1), A(2), A(3));
            c->fillPolys(c->strokePolys(sp, c->st.lineWidth), false, c->strokeSrc());
            break;
        }
        case C2D_CLEARRECT: c->clearRect(A(0), A(1), A(2), A(3)); break;
        case C2D_BEGINPATH: c->path.clear(); break;
        case C2D_CLOSEPATH: c->closePath(); break;
        case C2D_MOVETO: c->moveTo(A(0), A(1)); break;
        case C2D_LINETO: c->lineTo(A(0), A(1)); break;
        case C2D_QUADTO: c->quadTo(A(0), A(1), A(2), A(3)); break;
        case C2D_BEZIERTO: c->bezierTo(A(0), A(1), A(2), A(3), A(4), A(5)); break;
        case C2D_ARC: c->ellipse(A(0), A(1), A(2), A(2), 0, A(3), A(4), A(5) != 0); break;
        case C2D_ELLIPSE: c->ellipse(A(0), A(1), A(2), A(3), A(4), A(5), A(6), A(7) != 0); break;
        case C2D_ARCTO: c->arcTo(A(0), A(1), A(2), A(3), A(4)); break;
        case C2D_RECT: c->moveTo(A(0), A(1)); c->lineTo(A(0) + A(2), A(1)); c->lineTo(A(0) + A(2), A(1) + A(3)); c->lineTo(A(0), A(1) + A(3)); c->closePath(); break;
        case C2D_FILL: c->fillPolys(c->pathPolys(), A(0) != 0, c->fillSrc()); break;
        case C2D_STROKE: c->fillPolys(c->strokePolys(c->path, c->st.lineWidth), false, c->strokeSrc()); break;
        case C2D_SAVE: if (c->stack.size() < 64) c->stack.push_back(c->st); break;
        case C2D_RESTORE: if (!c->stack.empty()) { c->st = c->stack.back(); c->stack.pop_back(); } break;
        case C2D_TRANSLATE: { Mat m; m.e = A(0); m.f = A(1); c->st.m = c->st.m.mul(m); break; }
        case C2D_SCALE: { Mat m; m.a = A(0); m.d = A(1); c->st.m = c->st.m.mul(m); break; }
        case C2D_ROTATE: { Mat m; const double cs = std::cos(A(0)), sn = std::sin(A(0)); m.a = cs; m.b = sn; m.c = -sn; m.d = cs; c->st.m = c->st.m.mul(m); break; }
        case C2D_TRANSFORM: { Mat m; m.a = A(0); m.b = A(1); m.c = A(2); m.d = A(3); m.e = A(4); m.f = A(5); c->st.m = c->st.m.mul(m); break; }
        case C2D_SETTRANSFORM: { Mat m; m.a = A(0); m.b = A(1); m.c = A(2); m.d = A(3); m.e = A(4); m.f = A(5); c->st.m = m; break; }
        case C2D_RESETTRANSFORM: c->st.m = Mat(); break;
        case C2D_DRAWIMAGE: {
            Cv* s = cvOf(static_cast<int>(A(0)));
            if (!s) break;
            if (s == c) { Cv tmp(*s); c->drawImage(tmp, A(1), A(2), A(3), A(4), A(5), A(6), A(7), A(8)); }
            else c->drawImage(*s, A(1), A(2), A(3), A(4), A(5), A(6), A(7), A(8));
            break;
        }
        case C2D_LINEWIDTH: if (A(0) > 0) c->st.lineWidth = A(0); break;
        case C2D_ALPHA: if (A(0) >= 0 && A(0) <= 1) c->st.alpha = A(0); break;
        case C2D_LINECAP: c->st.cap = static_cast<int>(A(0)); break;
        case C2D_LINEJOIN: c->st.join = static_cast<int>(A(0)); break;
        case C2D_SMOOTH: c->st.smooth = A(0) != 0; break;
        case C2D_CLIP: {
            double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
            for (const SubPath& sp : c->path) for (const Pt& p : sp.p) { minx = std::min(minx, p.x); maxx = std::max(maxx, p.x); miny = std::min(miny, p.y); maxy = std::max(maxy, p.y); }
            if (minx > maxx) { c->st.cx1 = c->st.cx0; c->st.cy1 = c->st.cy0; break; }  // ruta vacia: nada visible
            c->st.cx0 = std::max(c->st.cx0, static_cast<int>(std::floor(minx))); c->st.cy0 = std::max(c->st.cy0, static_cast<int>(std::floor(miny)));
            c->st.cx1 = std::min(c->st.cx1, static_cast<int>(std::ceil(maxx))); c->st.cy1 = std::min(c->st.cy1, static_cast<int>(std::ceil(maxy)));
            if (c->st.cx1 < c->st.cx0) c->st.cx1 = c->st.cx0;
            if (c->st.cy1 < c->st.cy0) c->st.cy1 = c->st.cy0;
            break;
        }
        case C2D_FILLTEXT: c->drawText(A(0), A(1), A(2), false, c->fillSrc()); break;
        case C2D_STROKETEXT: c->drawText(A(0), A(1), A(2), true, c->strokeSrc()); break;
        case C2D_MEASURETEXT: return c->textWidth(c->text);
        case C2D_GRAD_LINEAR: { Grad g; g.x0 = A(0); g.y0 = A(1); g.x1 = A(2); g.y1 = A(3); if (c->grads.size() >= 256) return -1; c->grads.push_back(g); return static_cast<double>(c->grads.size() - 1); }
        case C2D_GRAD_RADIAL: { Grad g; g.radial = true; g.x0 = A(0); g.y0 = A(1); g.r0 = A(2); g.x1 = A(3); g.y1 = A(4); g.r1 = A(5); if (c->grads.size() >= 256) return -1; c->grads.push_back(g); return static_cast<double>(c->grads.size() - 1); }
        case C2D_GRAD_STOP: {
            const int gi = static_cast<int>(A(0));
            if (gi < 0 || gi >= static_cast<int>(c->grads.size())) break;
            auto& st = c->grads[gi].stops;
            st.push_back({std::max(0.0, std::min(1.0, A(1))), c->stopColor});
            std::stable_sort(st.begin(), st.end(), [](const std::pair<double, uint32_t>& p, const std::pair<double, uint32_t>& q) { return p.first < q.first; });
            break;
        }
        case C2D_FILLGRAD: c->st.fillGrad = static_cast<int>(A(0)); break;
        case C2D_STROKEGRAD: c->st.strokeGrad = static_cast<int>(A(0)); break;
        default: break;
    }
    return 0;
}

std::vector<uint8_t> c2d_get_pixels(int id, int x, int y, int w, int h) {
    std::vector<uint8_t> out;
    Cv* c = cvOf(id);
    if (!c || w <= 0 || h <= 0 || static_cast<long long>(w) * h > 12LL * 1000 * 1000) return out;
    out.assign(static_cast<size_t>(w) * h * 4, 0);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            const int sx = x + i, sy = y + j;
            if (sx < 0 || sy < 0 || sx >= c->w || sy >= c->h) continue;
            const uint32_t p = c->px[static_cast<size_t>(sy) * c->w + sx];
            uint8_t* o = &out[(static_cast<size_t>(j) * w + i) * 4];
            o[0] = (p >> 16) & 255; o[1] = (p >> 8) & 255; o[2] = p & 255; o[3] = p >> 24;
        }
    return out;
}
void c2d_put_pixels(int id, int x, int y, int w, int h, const uint8_t* rgba, size_t len) {
    Cv* c = cvOf(id);
    if (!c || w <= 0 || h <= 0 || len < static_cast<size_t>(w) * h * 4) return;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            const int dx = x + i, dy = y + j;
            if (dx < 0 || dy < 0 || dx >= c->w || dy >= c->h) continue;
            const uint8_t* o = rgba + (static_cast<size_t>(j) * w + i) * 4;
            c->px[static_cast<size_t>(dy) * c->w + dx] = (static_cast<uint32_t>(o[3]) << 24) | (static_cast<uint32_t>(o[0]) << 16) | (static_cast<uint32_t>(o[1]) << 8) | o[2];
        }
    c->used = true; c->dirty = true;
}

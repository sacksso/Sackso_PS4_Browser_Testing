#include "pagerender.h"

#include <algorithm>

#include "textengine.h"

int firstVisibleLine(const Document& d, int scroll) {
    int lo = 0, hi = static_cast<int>(d.lines.size());
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (d.lines[mid].y + d.lines[mid].h <= scroll) lo = mid + 1; else hi = mid;
    }
    return lo;
}

static uint32_t roleColor(const Theme& th, const Span& s) {
    uint32_t c = th.text;
    switch (s.role) {
        case ROLE_HEAD: c = th.head; break;
        case ROLE_LINK: c = th.link; break;
        case ROLE_DIM: c = th.dim; break;
        case ROLE_CODE: c = th.code; break;
        default: break;
    }
    if (s.color && s.role != ROLE_LINK) c = fixContrast(s.color, th.bg, c);
    return c;
}

static void drawCheck(Canvas& cv, const Theme& th, int x, int y, int sz, bool radio, bool on, bool hot) {
    const uint32_t border = hot ? th.accent : th.inputBorder;
    if (radio) {
        cv.circle(x + sz / 2.0f, y + sz / 2.0f, sz / 2.0f, border);
        cv.circle(x + sz / 2.0f, y + sz / 2.0f, sz / 2.0f - 2, th.inputBg);
        if (on) cv.circle(x + sz / 2.0f, y + sz / 2.0f, sz / 4.0f, th.accent);
    } else {
        cv.rrectBorder(x, y, sz, sz, 3, 2, border, on ? th.accent : th.inputBg);
        if (on) {
            cv.line(x + sz * 0.22f, y + sz * 0.52f, x + sz * 0.43f, y + sz * 0.74f, 2.2f, 0xFFFFFFFF);
            cv.line(x + sz * 0.43f, y + sz * 0.74f, x + sz * 0.80f, y + sz * 0.28f, 2.2f, 0xFFFFFFFF);
        }
    }
}

void drawDocument(Canvas& cv, const Document& d, const RenderCtx& rc) {
    const Theme& th = *rc.th;
    cv.setClip(rc.viewX, rc.viewY, rc.viewX + rc.viewW, rc.viewY + rc.viewH);
    cv.rect(rc.viewX, rc.viewY, rc.viewW, rc.viewH, th.bg);
    const int bottom = rc.scroll + rc.viewH;
    for (int li = firstVisibleLine(d, rc.scroll); li < static_cast<int>(d.lines.size()); li++) {
        const DocLine& L = d.lines[li];
        if (L.y >= bottom) break;
        const int top = rc.viewY + L.y - rc.scroll;
        const int base = top + L.baseline;
        for (const Span& s : L.spans) {
            const int sx = rc.contentLeft + s.x;
            if (sx > rc.viewX + rc.viewW || sx + s.w < rc.viewX) continue;
            const bool hot = s.focus >= 0 && (s.focus == rc.focus || s.focus == rc.hover);
            switch (s.kind) {
                case SK_TEXT: {
                    if (s.role == ROLE_CODE) cv.rect(sx - 2, top, s.w + 4, L.h, th.codeBg);
                    if (hot && s.role == ROLE_LINK) cv.rrect(sx - 3, top + 1, s.w + 6, L.h - 2, 4, th.focusBg);
                    const uint32_t col = roleColor(th, s);
                    te_draw(cv, sx, base, s.text, s.font, s.size, s.bold, col);
                    if (s.role == ROLE_LINK) cv.rect(sx, base + 2, s.w, hot ? 2 : 1, col);
                    break;
                }
                case SK_RULE:
                    cv.rect(sx, base - 3, s.w, 1, th.rule);
                    break;
                case SK_IMAGE: {
                    const int iy = base - s.h;
                    const ImageRef& im = d.images[s.ref];
                    if (im.state == 2 && !im.pix.empty()) {
                        cv.blit(im.pix.data(), im.pw, im.ph, sx + (s.w - im.pw) / 2, iy + (s.h - im.ph) / 2);
                    } else {
                        cv.rrectBorder(sx, iy, s.w, s.h, 4, 1, th.imgBorder, th.imgBg);
                        if (s.h >= 24 && s.w >= 40 && !im.alt.empty()) {
                            const std::string t = te_fit(im.alt, s.w - 12, FONT_SANS, 13, false);
                            te_draw(cv, sx + 6, iy + 18, t, FONT_SANS, 13, false, th.dim);
                        } else if (im.state == 1 && s.h >= 24) {
                            te_draw(cv, sx + 6, iy + 18, "...", FONT_SANS, 13, false, th.dim);
                        }
                    }
                    if (hot) cv.rrectBorder(sx - 2, iy - 2, s.w + 4, s.h + 4, 4, 2, th.accent, 0x00000000);
                    break;
                }
                case SK_INPUT: {
                    const FormField& f = d.fields[s.ref];
                    const bool editing = rc.editField == s.ref;
                    const int by = base - s.h + s.h / 4;
                    cv.rrectBorder(sx, by, s.w, s.h, 6, editing || hot ? 2 : 1, editing || hot ? th.accent : th.inputBorder, th.inputBg);
                    std::string shown = f.value;
                    uint32_t col = th.text;
                    if (f.type == "password") { std::string b; for (size_t i = 0; i < shown.size(); i++) b += "\xE2\x80\xA2"; shown = b; }
                    if (shown.empty() && !editing) { shown = f.placeholder; col = th.dim; }
                    const int px = std::max(12, s.size);
                    const int maxw = s.w - 20;
                    int off = 0;
                    const int fullW = te_width(shown, FONT_SANS, px, false);
                    if (editing && fullW > maxw) off = fullW - maxw;
                    cv.setClip(std::max(rc.viewX, sx + 8), std::max(rc.viewY, by), std::min(rc.viewX + rc.viewW, sx + s.w - 8), std::min(rc.viewY + rc.viewH, by + s.h));
                    const int tw = te_draw(cv, sx + 10 - off, by + s.h / 2 + px * 3 / 8, shown, FONT_SANS, px, false, col);
                    if (editing && rc.caretOn) cv.rect(sx + 10 - off + tw + 1, by + 6, 2, s.h - 12, th.text);
                    cv.setClip(rc.viewX, rc.viewY, rc.viewX + rc.viewW, rc.viewY + rc.viewH);
                    break;
                }
                case SK_BUTTON: {
                    const int by = base - s.h + s.h / 4;
                    cv.rrectBorder(sx, by, s.w, s.h, 6, hot ? 2 : 1, hot ? th.accent : th.btnBorder, th.btnBg);
                    const int px = std::max(12, s.size);
                    const int tw = te_width(s.text, FONT_SANS, px, false);
                    te_draw(cv, sx + (s.w - tw) / 2, by + s.h / 2 + px * 3 / 8, s.text, FONT_SANS, px, false, th.text);
                    break;
                }
                case SK_CHECK: {
                    const FormField& f = d.fields[s.ref];
                    drawCheck(cv, th, sx, base - s.h + s.h / 4, s.w, f.type == "radio", f.checked, hot);
                    break;
                }
            }
        }
    }
    cv.resetClip();
}

int hitFocus(const Document& d, const RenderCtx& rc, int mx, int my) {
    if (mx < rc.viewX || mx >= rc.viewX + rc.viewW || my < rc.viewY || my >= rc.viewY + rc.viewH) return -1;
    const int docY = my - rc.viewY + rc.scroll;
    int li = firstVisibleLine(d, docY);
    if (li >= static_cast<int>(d.lines.size())) return -1;
    const DocLine& L = d.lines[li];
    if (docY < L.y || docY >= L.y + L.h) return -1;
    for (const Span& s : L.spans) {
        if (s.focus < 0) continue;
        const int sx = rc.contentLeft + s.x;
        if (mx < sx - 3 || mx >= sx + s.w + 3) continue;
        if (s.kind == SK_TEXT) return s.focus;
        const int top = L.baseline + L.y - (s.kind == SK_IMAGE ? s.h : s.h - s.h / 4);
        if (docY >= top - 2 && docY < top + s.h + 2) return s.focus;
    }
    return -1;
}

int hitImage(const Document& d, const RenderCtx& rc, int mx, int my) {
    if (mx < rc.viewX || mx >= rc.viewX + rc.viewW || my < rc.viewY || my >= rc.viewY + rc.viewH) return -1;
    const int docY = my - rc.viewY + rc.scroll;
    const int li = firstVisibleLine(d, docY);
    if (li >= static_cast<int>(d.lines.size())) return -1;
    const DocLine& L = d.lines[li];
    if (docY < L.y || docY >= L.y + L.h) return -1;
    for (const Span& s : L.spans) {
        if (s.kind != SK_IMAGE || s.ref < 0 || s.ref >= static_cast<int>(d.images.size())) continue;
        const int sx = rc.contentLeft + s.x;
        if (mx < sx || mx >= sx + s.w) continue;
        const int top = L.baseline + L.y - s.h;
        if (docY >= top && docY < top + s.h) return s.ref;
    }
    return -1;
}

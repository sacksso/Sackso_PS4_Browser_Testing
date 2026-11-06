#include "inflate.h"

#include <cstring>
#include <vector>

namespace {

struct Bits {
    const uint8_t* p;
    size_t n, pos = 0;
    uint32_t buf = 0;
    int cnt = 0;
    bool over = false;
    Bits(const uint8_t* s, size_t len) : p(s), n(len) {}
    uint32_t get(int k) {  // k <= 16
        while (cnt < k) {
            if (pos >= n) { over = true; buf |= 0u; cnt += 8; continue; }
            buf |= static_cast<uint32_t>(p[pos++]) << cnt;
            cnt += 8;
        }
        const uint32_t v = buf & ((1u << k) - 1u);
        buf >>= k;
        cnt -= k;
        return v;
    }
    void alignByte() { buf = 0; cnt = 0; }  // descarta los bits sobrantes del byte actual (cnt < 8 tras lecturas completas)
};

struct Huff {
    uint16_t count[16];
    uint16_t symbol[288];
    bool build(const uint8_t* len, int n) {
        std::memset(count, 0, sizeof(count));
        for (int i = 0; i < n; i++) count[len[i]]++;
        count[0] = 0;
        int left = 1;
        for (int l = 1; l < 16; l++) {
            left <<= 1;
            left -= count[l];
            if (left < 0) return false;  // sobresuscrito
        }
        uint16_t offs[16];
        offs[1] = 0;
        for (int l = 1; l < 15; l++) offs[l + 1] = offs[l] + count[l];
        for (int i = 0; i < n; i++)
            if (len[i]) symbol[offs[len[i]]++] = static_cast<uint16_t>(i);
        return true;
    }
    int decode(Bits& b) const {
        int code = 0, first = 0, index = 0;
        for (int l = 1; l < 16; l++) {
            code |= static_cast<int>(b.get(1));
            const int c = count[l];
            if (code - c < first) return symbol[index + (code - first)];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

int codes(Bits& b, std::string& out, size_t maxOut, const Huff& lit, const Huff& dist) {
    for (;;) {
        const int sym = lit.decode(b);
        if (sym < 0 || b.over) return 1;
        if (sym < 256) {
            if (out.size() >= maxOut) return 2;
            out.push_back(static_cast<char>(sym));
        } else if (sym == 256) {
            return 0;
        } else {
            const int s = sym - 257;
            if (s >= 29) return 1;
            const int len = kLenBase[s] + static_cast<int>(b.get(kLenExtra[s]));
            const int ds = dist.decode(b);
            if (ds < 0 || ds >= 30 || b.over) return 1;
            const size_t d = kDistBase[ds] + b.get(kDistExtra[ds]);
            if (d > out.size()) return 1;
            for (int i = 0; i < len; i++) {
                if (out.size() >= maxOut) return 2;
                out.push_back(out[out.size() - d]);
            }
        }
    }
}

}  // namespace

int inflate_raw(const uint8_t* src, size_t n, std::string& out, size_t maxOut) {
    Bits b(src, n);
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    for (;;) {
        const uint32_t last = b.get(1);
        const uint32_t type = b.get(2);
        if (b.over) return 1;
        if (type == 0) {
            b.alignByte();
            if (b.pos + 4 > b.n) return 1;
            const uint32_t len = b.p[b.pos] | (b.p[b.pos + 1] << 8);
            const uint32_t nlen = b.p[b.pos + 2] | (b.p[b.pos + 3] << 8);
            b.pos += 4;
            if ((len ^ 0xFFFFu) != nlen) return 1;
            if (b.pos + len > b.n) return 1;
            const size_t room = maxOut > out.size() ? maxOut - out.size() : 0;
            const size_t take = len < room ? len : room;
            out.append(reinterpret_cast<const char*>(b.p + b.pos), take);
            b.pos += len;
            if (take < len) return 2;
        } else if (type == 1 || type == 2) {
            Huff* L = new Huff;
            Huff* D = new Huff;
            int rc = 0;
            if (type == 1) {
                uint8_t l[288];
                for (int i = 0; i < 144; i++) l[i] = 8;
                for (int i = 144; i < 256; i++) l[i] = 9;
                for (int i = 256; i < 280; i++) l[i] = 7;
                for (int i = 280; i < 288; i++) l[i] = 8;
                L->build(l, 288);
                uint8_t dl[30];
                for (int i = 0; i < 30; i++) dl[i] = 5;
                D->build(dl, 30);
            } else {
                const int nlen = static_cast<int>(b.get(5)) + 257;
                const int ndist = static_cast<int>(b.get(5)) + 1;
                const int ncode = static_cast<int>(b.get(4)) + 4;
                if (nlen > 286 || ndist > 30) { delete L; delete D; return 1; }
                uint8_t lengths[320];
                std::memset(lengths, 0, sizeof(lengths));
                for (int i = 0; i < ncode; i++) lengths[order[i]] = static_cast<uint8_t>(b.get(3));
                Huff* C = new Huff;
                if (!C->build(lengths, 19)) { delete C; delete L; delete D; return 1; }
                int idx = 0;
                std::memset(lengths, 0, sizeof(lengths));
                while (idx < nlen + ndist) {
                    const int sym = C->decode(b);
                    if (sym < 0 || b.over) { rc = 1; break; }
                    if (sym < 16) { lengths[idx++] = static_cast<uint8_t>(sym); continue; }
                    int rep, val = 0;
                    if (sym == 16) { if (idx == 0) { rc = 1; break; } val = lengths[idx - 1]; rep = 3 + static_cast<int>(b.get(2)); }
                    else if (sym == 17) rep = 3 + static_cast<int>(b.get(3));
                    else rep = 11 + static_cast<int>(b.get(7));
                    if (idx + rep > nlen + ndist) { rc = 1; break; }
                    while (rep--) lengths[idx++] = static_cast<uint8_t>(val);
                }
                delete C;
                if (rc == 0) {
                    if (!L->build(lengths, nlen) || !D->build(lengths + nlen, ndist)) rc = 1;
                }
            }
            if (rc == 0) rc = codes(b, out, maxOut, *L, *D);
            delete L;
            delete D;
            if (rc != 0) return rc;
        } else {
            return 1;
        }
        if (last) return b.over ? 1 : 0;
    }
}

int inflate_zlib(const uint8_t* src, size_t n, std::string& out, size_t maxOut) {
    if (n < 2) return 1;
    if ((src[0] & 0x0F) != 8 || ((src[0] << 8) | src[1]) % 31 != 0) return 1;
    size_t skip = 2;
    if (src[1] & 0x20) skip += 4;  // diccionario predefinido: no soportado de verdad, pero se salta el id
    if (n <= skip) return 1;
    return inflate_raw(src + skip, n - skip, out, maxOut);
}

int inflate_gzip(const uint8_t* src, size_t n, std::string& out, size_t maxOut) {
    if (n < 18 || src[0] != 0x1f || src[1] != 0x8b || src[2] != 8) return 1;
    const uint8_t flg = src[3];
    size_t p = 10;
    if (flg & 4) { if (p + 2 > n) return 1; p += 2 + (src[p] | (src[p + 1] << 8)); }
    if (flg & 8) { while (p < n && src[p]) p++; p++; }
    if (flg & 16) { while (p < n && src[p]) p++; p++; }
    if (flg & 2) p += 2;
    if (p >= n) return 1;
    return inflate_raw(src + p, n - p, out, maxOut);
}

int content_decode(const std::string& enc, const std::string& body, std::string& out, size_t maxOut) {
    std::string e;
    for (char c : enc) if (c != ' ') e += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    const uint8_t* s = reinterpret_cast<const uint8_t*>(body.data());
    std::string tmp;
    int rc;
    if (e == "gzip" || e == "x-gzip") rc = inflate_gzip(s, body.size(), tmp, maxOut);
    else if (e == "deflate") {
        rc = inflate_zlib(s, body.size(), tmp, maxOut);
        if (rc == 1) { tmp.clear(); rc = inflate_raw(s, body.size(), tmp, maxOut); }  // algunos servidores mandan deflate "crudo"
    } else return 3;
    if (rc == 0 || rc == 2) out.swap(tmp);
    return rc;
}

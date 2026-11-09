// g++ -std=c++17 -Wall -Wextra -Iapp tools/tests/test_neterr.cpp -o /tmp/test_neterr && /tmp/test_neterr
#include <cstdio>
#include "neterr.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)
static bool has(const std::string& s, const char* t) { return s.find(t) != std::string::npos; }
int main() {
    CHECK(has(neterr_explain(0x80431068u, "sendRequest"), "Tiempo agotado"));
    CHECK(has(neterr_explain(0x80431070u, ""), "TLS"));
    CHECK(has(neterr_explain(0, "resolve"), "No se encontro"));
    CHECK(has(neterr_explain(0, "connect"), "No se pudo conectar"));
    CHECK(has(neterr_explain(0x12345678u, "otro"), "no identificado"));
    CHECK(!neterr_explain(0x80431063u, "").empty());
    printf(fails ? "HAY FALLOS\n" : "test_neterr OK\n");
    return fails ? 1 : 0;
}

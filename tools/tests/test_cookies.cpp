// Pruebas en el PC del almacen de cookies (app/cookies.cpp).
#include "../../app/cookies.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)
static long long g_now = 1800000000;  // fijo: 15-ene-2027
int main() {
    cookies_set_clock([] { return g_now; });
    cookies_set_path("/tmp/psb_cookies_test.txt", "/tmp/psb_ua_test.txt");
    remove("/tmp/psb_cookies_test.txt"); remove("/tmp/psb_ua_test.txt");
    cookies_clear();

    // fechas
    CHECK(http_date_to_epoch("Thu, 01 Jan 1970 00:00:10 GMT") == 10);
    CHECK(http_date_to_epoch("Wed, 21 Oct 2026 07:28:00 GMT") == 1792567680);
    CHECK(http_date_to_epoch("Wed, 21-Oct-26 07:28:00 GMT") == 1792567680);
    CHECK(http_date_to_epoch("basura") == -1);

    // basicas: sesion, host-only, ruta por defecto
    CHECK(cookies_store_one("https://a.example.com/dir/page", "sid=abc123; HttpOnly"));
    CHECK(cookies_header("https://a.example.com/dir/otra") == "sid=abc123");
    CHECK(cookies_header("https://a.example.com/") == "");                    // ruta por defecto /dir
    CHECK(cookies_header("https://b.example.com/dir/x") == "");                // host-only
    CHECK(cookies_for_script("https://a.example.com/dir/x") == "");            // HttpOnly: invisible al script

    // Domain: sirve para subdominios; no se puede poner de otro sitio
    CHECK(cookies_store_one("https://www.example.com/", "pref=1; Domain=.example.com; Path=/"));
    CHECK(cookies_header("https://api.example.com/x") == "pref=1");
    CHECK(cookies_header("https://example.com/") == "pref=1");
    CHECK(!cookies_store_one("https://evil.com/", "x=1; Domain=example.com"));
    CHECK(!cookies_store_one("https://evil.com/", "x=1; Domain=com"));           // sufijo publico
    CHECK(cookies_header("https://evil.com/") == "");

    // Secure
    CHECK(!cookies_store_one("http://plain.test/", "s=1; Secure"));
    CHECK(cookies_store_one("https://sec.test/", "s=1; Secure; Path=/"));
    CHECK(cookies_header("https://sec.test/") == "s=1");
    CHECK(cookies_header("http://sec.test/") == "");

    // expiracion: Max-Age, Expires, borrado
    CHECK(cookies_store_one("https://exp.test/", "a=1; Max-Age=100; Path=/"));
    CHECK(cookies_header("https://exp.test/") == "a=1");
    g_now += 101;
    CHECK(cookies_header("https://exp.test/") == "");
    CHECK(cookies_store_one("https://exp.test/", "b=1; Expires=Wed, 21 Oct 2026 07:28:00 GMT; Path=/"));   // ya paso (2026 < 2027)
    CHECK(cookies_header("https://exp.test/") == "");
    CHECK(cookies_store_one("https://exp.test/", "c=1; Path=/"));
    CHECK(cookies_header("https://exp.test/") == "c=1");
    CHECK(cookies_store_one("https://exp.test/", "c=; Max-Age=0; Path=/"));
    CHECK(cookies_header("https://exp.test/") == "");

    // orden por ruta mas larga y reemplazo
    CHECK(cookies_store_one("https://o.test/", "k=raiz; Path=/"));
    CHECK(cookies_store_one("https://o.test/a/b", "k=hondo; Path=/a"));
    CHECK(cookies_header("https://o.test/a/x") == "k=hondo; k=raiz");
    CHECK(cookies_store_one("https://o.test/", "k=nuevo; Path=/"));
    CHECK(cookies_header("https://o.test/z") == "k=nuevo");
    CHECK(cookies_header("https://o.test/ab") == "k=nuevo");                   // /a no coincide con /ab

    // varias Set-Cookie en un bloque de cabeceras (con \r\n)
    const char* h = "HTTP/1.1 200 OK\r\nSet-Cookie: one=1; Path=/\r\nContent-Type: text/html\r\nset-cookie: two=2; Path=/; Secure\r\nSET-COOKIE: three=3; Path=/\r\n\r\n";
    CHECK(cookies_store_from_headers("https://multi.test/", h, strlen(h)) == 3);
    CHECK(cookies_header("https://multi.test/") == "one=1; two=2; three=3");

    // script
    cookies_set_from_script("https://js.test/p", "j=1; Path=/");
    cookies_set_from_script("https://js.test/p", "hh=1; Path=/; HttpOnly");   // un script no puede crear HttpOnly
    CHECK(cookies_for_script("https://js.test/p") == "j=1");

    // importar / exportar Netscape (incluye #HttpOnly_ y cf_clearance)
    cookies_clear();
    const std::string ns = "# Netscape HTTP Cookie File\n.sitio.test\tTRUE\t/\tTRUE\t1900000000\tcf_clearance\tTOKEN-LARGO.abc\n#HttpOnly_.sitio.test\tTRUE\t/\tTRUE\t0\t__cf_bm\tzzz\nhost.test\tFALSE\t/x\tFALSE\t1900000000\tq\tv\twith\ttabs\nvieja.test\tFALSE\t/\tFALSE\t5\tcaducada\t1\nmalformada\n";
    CHECK(cookies_import_netscape(ns) == 3);                                    // la caducada no entra
    CHECK(cookies_header("https://www.sitio.test/descarga") == "cf_clearance=TOKEN-LARGO.abc; __cf_bm=zzz");
    CHECK(cookies_for_script("https://www.sitio.test/") == "cf_clearance=TOKEN-LARGO.abc");
    CHECK(cookies_header("http://www.sitio.test/") == "");                      // Secure
    CHECK(cookies_header("http://host.test/x/y") == "q=v\twith\ttabs");
    const std::string ex = cookies_export_netscape();
    CHECK(ex.find("#HttpOnly_.sitio.test\tTRUE\t/\tTRUE\t0\t__cf_bm\tzzz") != std::string::npos);
    cookies_clear();
    CHECK(cookies_count() == 0);
    CHECK(cookies_import_netscape(ex) == 3);
    CHECK(cookies_header("https://sitio.test/") == "cf_clearance=TOKEN-LARGO.abc; __cf_bm=zzz");

    // importar cabecera Cookie
    cookies_clear();
    CHECK(cookies_import_header("descargas.test", "Cookie: PHPSESSID=abc; cf_clearance=xyz") == 2);
    CHECK(cookies_header("https://cdn.descargas.test/f") == "PHPSESSID=abc; cf_clearance=xyz");

    // persistencia
    cookies_save();
    cookies_clear();
    CHECK(cookies_count() == 0);
    cookies_load();
    CHECK(cookies_count() == 2);
    CHECK(cookies_header("https://descargas.test/") == "PHPSESSID=abc; cf_clearance=xyz");

    // borrar un dominio y sus subdominios
    cookies_store_one("https://otro.test/", "z=1; Path=/");
    cookies_clear("descargas.test");
    CHECK(cookies_header("https://descargas.test/") == "");
    CHECK(cookies_header("https://otro.test/") == "z=1");
    CHECK(cookies_domains().size() == 1);

    // User-Agent por dominio
    CHECK(ua_override_for("www.sitio.test").empty());
    ua_override_set("sitio.test", "Mozilla/5.0 PC");
    CHECK(ua_override_for("sitio.test") == "Mozilla/5.0 PC");
    CHECK(ua_override_for("a.b.sitio.test") == "Mozilla/5.0 PC");
    CHECK(ua_override_for("notsitio.test").empty());
    ua_override_set("a.sitio.test", "Otro");
    CHECK(ua_override_for("x.a.sitio.test") == "Otro");
    cookies_load();   // recarga desde disco
    CHECK(ua_override_for("a.sitio.test") == "Otro");
    ua_override_set("a.sitio.test", "");
    CHECK(ua_override_for("a.sitio.test") == "Mozilla/5.0 PC");

    // desactivadas
    g_cookiesEnabled = false;
    CHECK(cookies_header("https://otro.test/").empty());
    CHECK(!cookies_store_one("https://x.test/", "a=b"));
    g_cookiesEnabled = true;

    // url_split
    std::string sc, ho, pa;
    CHECK(url_split("https://User:pw@Host.TEST:8443/a/b?q=1#f", sc, ho, pa) && sc == "https" && ho == "host.test" && pa == "/a/b");
    CHECK(url_split("http://h.test", sc, ho, pa) && pa == "/");
    CHECK(!url_split("sinesquema", sc, ho, pa));
    printf(fails ? "\n%d FALLOS\n" : "\nTODO OK\n", fails);
    return fails ? 1 : 0;
}

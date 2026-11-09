// Compatibilidad JS con bibliotecas reales (jQuery...). Uso: test_jsx_compat <jquery.js> [otro.js ...]
// Carga una pagina que incluye cada biblioteca y ejecuta un script que las usa; imprime los errores [js].
#include "../../app/jsx.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
static uint32_t now(){ using namespace std::chrono; return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count(); }
static std::string slurp(const char* p){ std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static int fails = 0, jsErrors = 0;
#define CHECK(c) do{ if(!(c)){ printf("FALLO linea %d: %s\n",__LINE__,#c); fails++; } }while(0)
int main(int argc, char** argv){
  if (argc < 2) { printf("uso: %s jquery.js\n", argv[0]); return 2; }
  std::map<std::string,std::string> web;
  std::string tags;
  for (int i = 1; i < argc; i++) { std::string u = "https://x.test/lib" + std::to_string(i) + ".js"; web[u] = slurp(argv[i]); tags += "<script src=\"lib" + std::to_string(i) + ".js\"></script>\n"; }
  JsHost h; h.nowMs = now;
  h.log = [](const std::string& l){ printf("%s\n", l.c_str()); if (l.find("[js] ERROR") != std::string::npos) jsErrors++; };
  h.resolve = [](const std::string&, const std::string& r){ if (r.compare(0,4,"http")==0) return r; return std::string("https://x.test/") + r; };
  h.fetch = [&](const std::string& u, size_t, std::string& body, std::string& fin, int& st, std::string& ct){ auto it = web.find(u); if (it == web.end()) { st = 404; body = ""; fin = u; ct = ""; return true; } body = it->second; fin = u; st = 200; ct = "application/javascript"; return true; };
  static std::string jar;
  h.getCookies = [](const std::string&){ return jar; };
  h.setCookie = [](const std::string&, const std::string& c){ jar += (jar.empty() ? "" : "; ") + c.substr(0, c.find(';')); };
  h.blocked = [](const std::string& u){ return u.find("/ads.js") != std::string::npos; };
  web["https://x.test/ads.js"] = "throw new Error('NO DEBE EJECUTARSE');";
  tags += "<script src=\"ads.js\"></script>\n";
  jsx_set_host(h);
  std::string page = "<!DOCTYPE html><html><head><title>t</title></head><body><div id=\"a\" class=\"x\">hola</div><ul id=\"l\"><li>1</li><li>2</li></ul>\n" + tags + R"(<script>
if (window.jQuery) {
  var $a = jQuery('#a');
  $a.text('jquery ok').addClass('y');
  jQuery('#l').append('<li class="n">3</li>');
  jQuery('<p id="made">creado</p>').appendTo('body');
  jQuery('#l li').each(function(i){ jQuery(this).attr('data-i', i); });
  jQuery(function(){ jQuery('body').append('<i id="ready">listo</i>'); });
  jQuery.ajax && console.log('ajax presente');
  console.log('n li', jQuery('#l li').length, 'hasClass', $a.hasClass('y'));
} else console.log('SIN jQuery');
document.cookie = 'visto=1; Path=/'; console.log('cookie: ' + document.cookie);
try { console.log('crypto', typeof crypto.getRandomValues, crypto.randomUUID().length); } catch(e) { console.log('crypto FALLA ' + e); }
try { console.log('shadow', typeof ShadowRoot, typeof NodeList, typeof HTMLCollection, typeof MediaError, typeof DOMParser, typeof TextEncoder); } catch(e) { console.log('globals FALLA ' + e); }
try { var d = document.implementation.createHTMLDocument(''); d.body.innerHTML = '<b>x</b>'; console.log('impl', d.body.childNodes.length, d.body.firstChild.nodeName); } catch(e) { console.log('implementation FALLA ' + e); }
</script></body></html>)";
  JsxInfo info;
  std::string out = jsx_load("https://x.test/index.html", page, true, &info);
  jsx_attach((void*)1);
  for (int i = 0; i < 40; i++) {   // los callbacks de jQuery.ready van en temporizadores: se mueven como lo hace el bucle principal
    std::string more; const uint32_t t = now();
    if (jsx_pump((void*)1, t, &more)) out = more;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  printf("---- salida ----\n%s\n", out.c_str());
  CHECK(out.find("jquery ok") != std::string::npos);
  CHECK(out.find("creado") != std::string::npos);
  CHECK(out.find("listo") != std::string::npos);
  CHECK(out.find("<li class=\"n\">3</li>") != std::string::npos || out.find("class=\"n\"") != std::string::npos);
  CHECK(info.scriptErrors == 0);
  CHECK(jar == "visto=1");
  printf("%s (errores js=%d, fallos=%d)\n", fails || jsErrors ? "HAY PROBLEMAS" : "TODO OK", jsErrors, fails);
  return fails ? 1 : 0;
}

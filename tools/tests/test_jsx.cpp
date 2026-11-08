// Prueba en el PC de dom + jsx con QuickJS real. Uso: test_jsx
#include "../../app/jsx.h"
#include <cstdio>
#include <chrono>
#include <map>
#include <string>
static uint32_t now(){ using namespace std::chrono; return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count(); }
static int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FALLO linea %d: %s\n",__LINE__,#c); fails++; } }while(0)
int main(){
  std::map<std::string,std::string> web;
  web["https://x.test/app.js"]="document.getElementById('t').textContent='desde externo'; window.__ext=1;";
  web["https://x.test/s.css"]=".oculto{display:none} #t{color:red} .a .b{display:none} p:first-child{color:blue}";
  web["https://x.test/data.json"]="{\"n\":42}";
  JsHost h;
  h.nowMs=now;
  h.log=[](const std::string&l){ printf("%s\n",l.c_str()); };
  h.resolve=[](const std::string&b,const std::string&r){ if(r.compare(0,4,"http")==0) return r; return std::string("https://x.test/")+r; };
  h.fetch=[&](const std::string&u,size_t,std::string&body,std::string&fin,int&st,std::string&ct){ auto it=web.find(u); if(it==web.end()){st=404;body="";fin=u;ct="";return true;} body=it->second;fin=u;st=200;ct="text/plain";return true; };
  jsx_set_host(h);
  const char* page = R"(<!DOCTYPE html><html><head><title>Prueba &amp; ok</title><link rel="stylesheet" href="s.css"><style>.s{color:green}</style></head>
<body><h1 id="t">original</h1><p class="oculto">NO DEBE VERSE</p><div class="a"><span class="b">oculto2</span><span class="c">visible c</span></div>
<ul><li>uno<li>dos</ul><noscript>sin js</noscript>
<script src="app.js"></script>
<script>
 var out=[];
 document.querySelector('#t').setAttribute('data-x','1');
 var d=document.createElement('div'); d.className='s'; d.textContent='creado & <ok>'; document.body.appendChild(d);
 out.push(document.querySelectorAll('li').length);
 out.push(document.getElementsByClassName('c').length);
 document.title='nuevo titulo';
 out.push(document.querySelector('.s').classList.contains('s'));
 d.classList.add('oculto');       // ahora debe ocultarse por CSS
 setTimeout(function(){ document.body.insertAdjacentHTML('beforeend','<p id="late">tarde</p>'); },30);
 var iv=setInterval(function(){ window.__n=(window.__n||0)+1; if(window.__n>=3) clearInterval(iv); },10);
 Promise.resolve(1).then(function(){ document.body.setAttribute('data-p','ok'); });
 fetch('data.json').then(function(r){return r.json();}).then(function(j){ var e=document.createElement('p'); e.id='fj'; e.textContent='json '+j.n; document.body.appendChild(e); });
 console.log('out', JSON.stringify(out), typeof requestAnimationFrame, location.hostname, new URL('/a?b=1','https://q.test').search, btoa('hola'), atob('aG9sYQ=='));
 document.addEventListener('DOMContentLoaded', function(){ console.log('DCL ok'); var q=document.createElement('p'); q.id='dcl'; q.textContent='dcl'; document.body.appendChild(q); });
 window.addEventListener('load', function(){ console.log('load ok'); });
 el=document.createElement('button'); var n=0; el.addEventListener('click',function(){n++;}); document.body.appendChild(el); el.click(); el.click(); console.log('clicks',n);
 var bad = document.querySelector('a[');
</script>
<script>throw new Error('boom');</script>
<script>console.log('sigue tras error', ds=document.body.dataset.p);</script>
</body></html>)";
  JsxInfo info;
  std::string out=jsx_load("https://x.test/index.html", page, true, &info);
  printf("---- HTML resultante (inicial)\n%s\n----\n", out.c_str());
  CHECK(out.find("NO DEBE VERSE")==std::string::npos);
  CHECK(out.find("oculto2")==std::string::npos);
  CHECK(out.find("visible c")!=std::string::npos);
  CHECK(out.find("desde externo")!=std::string::npos);
  CHECK(out.find("creado")==std::string::npos);   // oculto por classList.add('oculto')
  CHECK(out.find("sin js")==std::string::npos);
  CHECK(out.find("sin js")==std::string::npos);
  CHECK(out.find("color:red")!=std::string::npos);
  CHECK(out.find("dcl")!=std::string::npos);
  CHECK(info.scripts==2 && info.scriptErrors==2);
  jsx_attach((void*)1);
  std::string h2; bool got=false;
  uint32_t t0=now();
  while(now()-t0<500){ std::string o; if(jsx_pump((void*)1, now(), &o)){ h2=o; got=true; } }
  printf("---- HTML tras timers\n%s\n", h2.c_str());
  CHECK(got);
  CHECK(h2.find("tarde")!=std::string::npos);
  CHECK(h2.find("json 42")!=std::string::npos);
  CHECK(h2.find("data-p=\"ok\"")!=std::string::npos);
  jsx_close((void*)1);
  printf(fails? "\n%d FALLOS\n":"\nTODO OK\n", fails);
  return fails?1:0;
}

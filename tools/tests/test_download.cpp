#include "download.h"
#include <cassert>
#include <cstdio>
#include <iostream>
int main(){
  using namespace std;
  #define EQ(a,b) do{ string x=(a), y=(b); if(x!=y){ printf("FALLO linea %d: '%s' != '%s'\n",__LINE__,x.c_str(),y.c_str()); return 1;} }while(0)
  EQ(dl_filename("https://x.com/a/b/foo%20bar.zip?x=1","","application/zip"),"foo bar.zip");
  EQ(dl_filename("https://x.com/download.php?id=3","attachment; filename=\"mi juego.pkg\"","application/octet-stream"),"mi juego.pkg");
  EQ(dl_filename("https://x.com/d","attachment; filename=\"a.txt\"; filename*=UTF-8''%C3%A1%20b.txt","text/plain"),"\xC3\xA1 b.txt");
  EQ(dl_filename("https://x.com/get.php?id=3","","application/pdf"),"get.pdf");
  EQ(dl_filename("https://x.com/","","image/png"),"descarga.png");
  EQ(dl_filename("https://x.com/img/1234","","image/jpeg"),"1234.jpg");
  EQ(dl_filename("https://x.com/..%2F..%2Fetc%2Fpasswd","",""),"_.._etc_passwd");  // sin rutas
  EQ(dl_filename("https://x.com/a/..","",""),"descarga");
  EQ(dl_sanitize_name("../../x/y:z*?.zip"),"_.._x_y_z__.zip");
  EQ(dl_sanitize_name("  ...oculto.txt..."),"oculto.txt");
  string big(300,'a'); big+=".mp4"; string s=dl_sanitize_name(big); assert(s.size()==120 && s.substr(116)==".mp4");
  string u; for(int i=0;i<70;i++) u+="\xC3\xA9"; u+=".zip"; s=dl_sanitize_name(u); assert(s.size()<=120); 
  { // utf8 valido
    size_t i=0; while(i+4<s.size()){ unsigned char c=s[i]; assert(c==0xC3); assert((unsigned char)s[i+1]==0xA9); i+=2; } }
  assert(dl_is_page_type("")&&dl_is_page_type("text/html; charset=utf-8")&&dl_is_page_type("application/json")&&dl_is_page_type("application/xhtml+xml"));
  assert(!dl_is_page_type("application/zip")&&!dl_is_page_type("image/png")&&!dl_is_page_type("video/mp4")&&!dl_is_page_type("application/octet-stream"));
  assert(dl_is_previewable_image("image/png")&&dl_is_previewable_image("image/jpeg")&&!dl_is_previewable_image("image/webp")&&!dl_is_previewable_image("image/gif"));
  assert(dl_is_streaming("application/vnd.apple.mpegurl","https://a/b")&&dl_is_streaming("","https://a/b/master.m3u8?x=1")&&!dl_is_streaming("video/mp4","https://a/b.mp4"));
  assert(dl_is_attachment("Attachment; filename=x")&&!dl_is_attachment("inline")&&!dl_is_attachment(""));
  EQ(dl_format_size(0),"0 B"); EQ(dl_format_size(1536),"1.5 KB"); EQ(dl_format_size(5LL*1024*1024+300000),"5.3 MB"); EQ(dl_format_size(3LL<<30),"3.00 GB"); EQ(dl_format_size(-1),"?");
  // unico
  system("rm -rf /tmp/dltest && mkdir -p /tmp/dltest && touch /tmp/dltest/a.zip /tmp/dltest/a\\ \\(1\\).zip /tmp/dltest/noext");
  EQ(dl_unique_path("/tmp/dltest","a.zip"),"/tmp/dltest/a (2).zip"); EQ(dl_unique_path("/tmp/dltest","b.zip"),"/tmp/dltest/b.zip"); EQ(dl_unique_path("/tmp/dltest","noext"),"/tmp/dltest/noext (1)");
  EQ(dl_type_label("image/png"),"Imagen (png)"); EQ(dl_type_label("application/zip; x=1"),"Archivo comprimido");
  puts("todo ok"); return 0; }

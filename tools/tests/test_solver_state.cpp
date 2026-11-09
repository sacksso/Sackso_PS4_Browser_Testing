// Lee la salida de /state de psb_solver.py (archivo) y comprueba que el almacen de cookies la entiende. Uso: test_solver_state estado.txt https://dominio/
#include "../../app/cookies.h"
#include <cstdio>
#include <fstream>
#include <sstream>
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    cookies_set_path("/tmp/psb_c_x.txt", "/tmp/psb_u_x.txt");
    std::ifstream f(argv[1]); std::stringstream ss; ss << f.rdbuf();
    const int n = cookies_import_netscape(ss.str());
    const std::string h = cookies_header(argv[2]);
    printf("importadas=%d cabecera='%s' scriptVisible='%s'\n", n, h.c_str(), cookies_for_script(argv[2]).c_str());
    return (n >= 1 && h.find("cf_clearance=FAKE") != std::string::npos && cookies_for_script(argv[2]).empty()) ? 0 : 1;
}

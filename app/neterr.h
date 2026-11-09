#pragma once
// v0.9.3+: explica en castellano claro un error de red (codigo SCE + paso). Funcion pura, se prueba en PC (tools/tests/test_neterr.cpp).
// ATENCION: los significados de los codigos 0x8043xxxx salen de la memoria del autor (sceHttp), no de documentacion oficial; se
// marcan como "probable" cuando no estan confirmados con un log real. Confirmado con un log real: 0x80431068 en sendRequest (timeout).
#include <string>

inline std::string neterr_explain(unsigned code, const std::string& stage) {
    switch (code) {
        case 0x80431068u: return "Tiempo agotado: el servidor no respondio. Comprueba que la direccion es correcta y que tienes conexion; si es un servidor lento o caido, prueba mas tarde.";
        case 0x80431063u: return "Error de red: no se pudo comunicar con el servidor (conexion cortada o sin Internet).";
        case 0x80431061u: return "Direccion no valida: el protocolo no es http ni https.";
        case 0x80431064u: return "El servidor dio una respuesta que la consola no entiende (probable).";
        case 0x80431070u: return "Error de seguridad (TLS/SSL): el certificado o el cifrado del sitio no son compatibles con la consola (probable).";
        case 0x80431080u: return "La carga se cancelo.";
        default: break;
    }
    auto has = [&](const char* s) { return stage.find(s) != std::string::npos; };
    if (has("resolve") || has("dns") || has("Resolve")) return "No se encontro el servidor: revisa la direccion o el DNS de la consola.";
    if (has("connect") || has("Connect")) return "No se pudo conectar con el servidor: puede estar caido o bloqueado en tu red.";
    if (has("ssl") || has("tls") || has("Ssl")) return "Error de seguridad (TLS/SSL) al conectar con el sitio.";
    return "Error de red no identificado: reintenta; si se repite, mira el log.";
}

#pragma once
// Utilidades puras de descargas (sin SDL ni red, se prueban en PC): decidir si un contenido se muestra o se descarga,
// sacar un nombre de archivo seguro de las cabeceras/URL y dar formato a tamanos.
#include <string>

// true si el visor sabe mostrar este Content-Type (vacio, html, texto, json, xml, javascript).
bool dl_is_page_type(const std::string& contentType);
// true si es una imagen que el visor decodifica (PNG, JPEG, BMP) y por tanto se puede previsualizar.
bool dl_is_previewable_image(const std::string& contentType);
// true si es una lista de reproduccion por segmentos (HLS .m3u8 / DASH .mpd): no es un archivo descargable tal cual.
bool dl_is_streaming(const std::string& contentType, const std::string& url);
// true si Content-Disposition pide descargar (attachment).
bool dl_is_attachment(const std::string& contentDisposition);

// Nombre seguro (sin rutas, sin caracteres raros, maximo ~120 bytes). Vacio si no queda nada.
std::string dl_sanitize_name(const std::string& raw);
// Nombre de archivo a partir de Content-Disposition (filename* / filename), si no de la URL; anade la extension del
// Content-Type si el nombre no tiene. Nunca devuelve vacio.
std::string dl_filename(const std::string& url, const std::string& contentDisposition, const std::string& contentType);
// Ruta dir/nombre que no exista todavia: si existe, "nombre (1).ext", "nombre (2).ext"...
std::string dl_unique_path(const std::string& dir, const std::string& name);

std::string dl_format_size(long long bytes);   // "512 B", "1.5 KB", "23.4 MB", "1.2 GB"
std::string dl_type_label(const std::string& contentType);  // etiqueta legible ("Imagen PNG", "Video MP4"...)

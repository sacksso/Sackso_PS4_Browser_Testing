#pragma once
// Log compartido: se escribe en /data/psbrowser/log.txt y por stdout. Definido en main.cpp.
void psb_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

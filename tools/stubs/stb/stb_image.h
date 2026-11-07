#pragma once
extern "C" { int stbi_info_from_memory(const unsigned char*, int, int*, int*, int*); unsigned char* stbi_load_from_memory(const unsigned char*, int, int*, int*, int*, int); void stbi_image_free(void*); }

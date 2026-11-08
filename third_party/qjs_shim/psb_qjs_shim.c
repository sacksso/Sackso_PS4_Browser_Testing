#include <stddef.h>
/* Ver malloc_np.h: el asignador de QuickJS que se usa de verdad es el de jsx.cpp. */
size_t psb_malloc_usable_size(const void* p) { (void)p; return 0; }

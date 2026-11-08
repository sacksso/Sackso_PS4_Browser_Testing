#pragma once
/* Sustituto de <malloc_np.h> (FreeBSD) para compilar QuickJS-ng con OpenOrbis, cuyo sysroot no lo trae.
   QuickJS solo lo usa para malloc_usable_size en su asignador por defecto; psbrowser usa su propio asignador (jsx.cpp),
   asi que esta funcion solo tiene que existir para enlazar. */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
size_t psb_malloc_usable_size(const void* p);
#ifdef __cplusplus
}
#endif
#define malloc_usable_size(p) psb_malloc_usable_size(p)

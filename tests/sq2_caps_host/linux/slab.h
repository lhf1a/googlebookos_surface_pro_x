/* Host-only shim selected explicitly by the capability transaction test. */
#ifndef SQ2_CAPS_TEST_SLAB_H
#define SQ2_CAPS_TEST_SLAB_H
#include <stdlib.h>
#define GFP_KERNEL 0
#define kzalloc(n, flags) calloc(1, (n))
#define kfree(p) free(p)
#endif

/* HIDL is a platform ABI using std::__1, not the NDK std::__ndk1 ABI.
 * Preserve the toolchain's other libc++ settings; do not edit NDK headers. */
#include <__config_site>
#undef _LIBCPP_ABI_NAMESPACE
#define _LIBCPP_ABI_NAMESPACE __1

/* HIDL is a platform ABI using std::__1, not the NDK std::__ndk1 ABI.
 * Preserve the toolchain's other libc++ settings; do not edit NDK headers. */
#include <__config_site>
#undef _LIBCPP_ABI_NAMESPACE
#define _LIBCPP_ABI_NAMESPACE __1

/* Android VNDK34 platform libc++ does not export the LLVM19 additional
 * stringstream explicit instantiations. Instantiate their real header bodies
 * locally instead of asking that older runtime for absent vtables/functions.
 * This does not change libc++ layout/namespace or substitute implementations.
 */
#include <__availability>
#undef _LIBCPP_AVAILABILITY_HAS_ADDITIONAL_IOSTREAM_EXPLICIT_INSTANTIATIONS_1
#define _LIBCPP_AVAILABILITY_HAS_ADDITIONAL_IOSTREAM_EXPLICIT_INSTANTIATIONS_1 0

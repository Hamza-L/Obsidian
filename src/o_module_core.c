#include "core/o_memory.c"

#if defined(__APPLE__)
#include "core/platform/macos/o_p_alloc.c"
#elif defined(_WIN32)
#include "core/platform/win32/o_p_alloc.c"
#endif

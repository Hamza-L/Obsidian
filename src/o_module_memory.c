#include "memory/o_memory.c"

#if defined(__APPLE__)
#include "memory/platform/macos/o_p_alloc.c"
#elif defined(_WIN32)
#include "memory/platform/win32/o_p_alloc.c"
#endif

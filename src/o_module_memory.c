#include "memory/o_memory.c"

#if defined(__APPLE__)
#include "memory/platform/macos/m_p_alloc.c"
#elif defined(_WIN32)
#include "memory/platform/win32/m_p_alloc.c"
#endif

#include "memory/m_alloc.c"

#if defined(__APPLE__)
#include "memory/m_alloc_macos.c"
#elif defined(_WIN32)
#include "memory/m_alloc_win32.c"
#endif

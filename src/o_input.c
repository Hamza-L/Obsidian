#include "input/i_input.c"

#if defined(__APPLE__)
#include "input/platform/macos/i_p_input.m"
#elif defined(_WIN32)
#include "input/platform/win32/i_p_input.c"
#endif

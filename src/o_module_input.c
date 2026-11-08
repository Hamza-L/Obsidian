#include "input/o_input.c"

#if defined(__APPLE__)
#include "input/platform/macos/o_p_input.m"
#elif defined(_WIN32)
#include "input/platform/win32/o_p_input.c"
#endif

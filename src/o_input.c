#include "input/i_input.c"

#if defined(__APPLE__)
#include "platform/macos/p_input.m"
#elif defined(_WIN32)
#include "platform/win32/p_input.c"
#endif

#if defined(__APPLE__)
#include "platform/macos/p_renderer.m"
#elif defined(_WIN32)
#include "platform/win32/p_vulkan_loader.c"
#include "platform/win32/p_renderer.c"
#include "platform/win32/p_swapchain_dxgi.c"
#endif

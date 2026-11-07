#include "renderer/r_renderer.c"

#if defined(__APPLE__)
#include "renderer/backend/metal/r_b_renderer.m"
#elif defined(_WIN32)
#include "renderer/backend/vulkan/r_b_vulkan_loader.c"
#include "renderer/backend/vulkan/r_b_renderer.c"
#include "renderer/backend/vulkan/r_b_renderer_shader.c"
#include "renderer/backend/vulkan/platform/win32/r_b_p_swapchain.c"
#endif

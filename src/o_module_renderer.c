#include "renderer/o_renderer.c"

#if defined(__APPLE__)
#include "renderer/backend/metal/o_b_renderer.m"
#elif defined(_WIN32)

#ifdef GFX_BACKEND_D3D11
#include "renderer/backend/d3d11/o_b_renderer.c"
#else
#include "renderer/backend/vulkan/o_b_vulkan_loader.c"
#include "renderer/backend/vulkan/o_b_renderer.c"
#include "renderer/backend/vulkan/o_b_renderer_shader.c"
#include "renderer/backend/vulkan/platform/win32/o_b_p_swapchain.c"
#endif //GFX_BACKEND_D3D11
#endif //defined(__APPLE__)

#include "o_renderer.h"
#include "p_vulkan_utils.h"

#include "o_log.h"

#include <stdlib.h>

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

struct ORenderer {
    VkContext* ctx;
};

ORenderer *renderer_create(void) {
    ORenderer* renderer = malloc(sizeof(ORenderer));
    if (!renderer) {
        LOG_ERROR("renderer_create(): Failed to alloc memory");
        return NULL;
    }

    renderer->ctx = vk_context_create((VkContextDesc){
        .enableValidationLayer = true,
        .requiredInstanceExts =
            {
                VK_KHR_SURFACE_EXTENSION_NAME,
                VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
            },
        .requiredDeviceExts = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
            // VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
            // VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME,
            // VK_KHR_DEVICE_ADDRESS_COMMANDS_EXTENSION_NAME,
            // VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME
        }
    });

    if (!renderer->ctx) {
        LOG_ERROR("renderer_create(): Failed to create vulkan context");
        free(renderer);
        return NULL;
    }
    return renderer;
}

void renderer_destroy(ORenderer *renderer) {
    if (!renderer) return;
    if (renderer->ctx) vk_context_destroy(renderer->ctx);
    free(renderer);
}

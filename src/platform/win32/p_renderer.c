#include "o_renderer.h"
#include "p_vulkan_utils.h"

#include "o_log.h"

#include <stdlib.h>

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

struct ORenderer {
    bool isOnline;
    VkInstance instance;
};

ORenderer *renderer_create(void) {
    ORenderer* renderer = malloc(sizeof(ORenderer));
    if (!renderer) LOG_ERROR("renderer_create(): Failed to alloc memory");
    *renderer = (ORenderer){.isOnline = true, .instance = VK_NULL_HANDLE};

    renderer->instance = vk_instance_create((VkInstanceDesc){
	    .enableValidationLayer = true,
	    .requiredExts = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
            // VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
            // VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME,
            // VK_KHR_DEVICE_ADDRESS_COMMANDS_EXTENSION_NAME,
            // VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME
        }
	});

    if (!renderer->instance) LOG_ERROR("renderer_create(): Failed to create vulkan context");
    return renderer;
}

void renderer_destroy(ORenderer *renderer) {
    vk_instance_destroy(&renderer->instance);
    free(renderer);
}

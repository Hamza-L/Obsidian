#ifndef P_VULKAN_LAYER
#define P_VULKAN_LAYER

#include <stdbool.h>

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "p_vulkan_limits.h"

typedef struct VkContext VkContext;

typedef struct {
    bool enableValidationLayer;
    const char *requiredInstanceExts[MAX_REQUESTED_INSTANCE_EXT];
    const char *requiredDeviceExts[MAX_REQUESTED_DEVICE_EXT];
} VkContextDesc;

VkContext *vk_context_create(VkContextDesc desc);
void vk_context_destroy(VkContext *ctx);

#endif //P_VULKAN_LAYER

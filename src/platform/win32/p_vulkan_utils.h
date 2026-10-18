#ifndef P_VULKAN_LAYER
#define P_VULKAN_LAYER

#include <stdbool.h>

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

typedef struct {
    bool enableValidationLayer;
    const char *requiredExts[8];
} VkInstanceDesc;

VkInstance vk_instance_create(VkInstanceDesc desc);
void vk_instance_destroy(VkInstance* instance);

#endif //P_VULKAN_LAYER

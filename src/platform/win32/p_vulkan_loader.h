#ifndef P_VULKAN_LOADER_H
#define P_VULKAN_LOADER_H

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <stdbool.h>

void *vk_get_instance(VkInstance instance, const char *name);
void *vk_get_device(VkDevice device, const char *name);
bool ne_vk_load_instance_fns(VkInstance instance);
bool ne_vk_load_device_fns(VkDevice device);
bool ne_vk_load_loader(HMODULE* vulkan_lib_handle) ;

#endif //P_VULKAN_LOADER_H

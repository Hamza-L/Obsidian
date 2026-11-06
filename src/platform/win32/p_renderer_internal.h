#ifndef P_RENDERER_INTERNAL_H
#define P_RENDERER_INTERNAL_H
#include "o_renderer.h"

#include "p_vulkan_limits.h"
#include "p_swapchain.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#define MAX_LAYER_COUNT 8
#define MAX_SHADER_COUNT 256

typedef void* MPool;
typedef struct OShaderSlot OShaderSlot;
typedef struct OImageSlot OImageSlot;

struct ORenderer {
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physDevice;

    VkCommandPool cmdPool;
    VkCommandBuffer cmdBuffer[MAX_FRAMES_IN_FLIGHT];
    VkFence fences[MAX_FRAMES_IN_FLIGHT];

    struct {
        VkQueue graphics;
        VkQueue transfer;
        uint32_t graphicsqfi;
        uint32_t transferqfi;
    } queue;

    struct {
        struct {
            void *hwnd;
            Swapchain *swapchain;
            uint32_t age;
        } data[MAX_LAYER_COUNT] ;
    } layerList;

    MPool shaderPool;
    MPool imagePool;

    HMODULE vulkanLib;
};

struct OShaderSlot{
    OShaderDesc desc;
    VkShaderModule module;
    bool compiled;
};

struct OImageSlot{
    OImageDesc desc;
    VkImage image;
    VkImageView imageView;
};

#endif //P_RENDERER_INTERNAL

#ifndef O_B_RENDERER_INTERNAL_H
#define O_B_RENDERER_INTERNAL_H
#include "o_renderer.h"
#include "o_memory.h"

#include "renderer/o_renderer_internal.h"
#include "renderer/backend/vulkan/o_b_vulkan_loader.h"
#include "renderer/backend/vulkan/o_b_vulkan_limits.h"
#include "renderer/backend/vulkan/platform/win32/o_b_p_swapchain.h"


#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define MAX_LAYER_COUNT 8
#define MAX_SHADER_COUNT 256

typedef struct OShaderSlot OShaderSlot;
typedef struct OImageSlot OImageSlot;

struct ORenderer {
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physDevice;

    VkCommandPool cmdPool;
    VkCommandBuffer cmdBuffer[MAX_FRAMES_IN_FLIGHT];
    VkFence fences[MAX_FRAMES_IN_FLIGHT];
    uint64_t frameIndex;
    uint32_t presentMask;

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
    OShaderStage stage;
    VkShaderModule module;
    bool compiled;
};

struct OImageSlot{
    OImageDesc desc;
    VkImage image;
    VkImageView imageView;
};

#endif //O_B_RENDERER_INTERNAL_H

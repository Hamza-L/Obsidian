#ifndef SWAPCHAIN_H
#define SWAPCHAIN_H

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct Swapchain Swapchain;

typedef enum SwapchainPresentResult {
    SWAPCHAIN_PRESENT_SUCCESS = 0,
    SWAPCHAIN_PRESENT_OUT_OF_DATE,
    SWAPCHAIN_PRESENT_FAILED,
} SwapchainPresentResult;

// Values for the Vulkan submit that renders the next presented frame into shared[imageIndex].
typedef struct SwapchainFrameSync {
    VkSemaphore waitSemaphore;
    uint64_t waitValue;
    VkSemaphore signalSemaphore;
    uint64_t signalValue;
    uint32_t imageIndex;
} SwapchainFrameSync;

typedef struct SwapchainDesc {
    VkDevice device;
    VkPhysicalDevice physDevice;
    VkInstance instance;
    bool vsync;
} SwapchainDesc;

Swapchain *swapchain_create(void *native_window_handle, SwapchainDesc desc);
bool swapchain_resize(Swapchain *sc, uint32_t width, uint32_t height);
void swapchain_destroy(Swapchain *sc);

void swapchain_wait_for_frame(Swapchain *sc);
SwapchainFrameSync swapchain_frame_sync(const Swapchain *sc);
bool swapchain_is_lost(const Swapchain *sc);
SwapchainPresentResult swapchain_present(Swapchain *sc);

#endif /* SWAPCHAIN_H */

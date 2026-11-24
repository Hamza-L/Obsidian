#ifndef O_B_P_SWAPCHAIN_H
#define O_B_P_SWAPCHAIN_H

#include "renderer/backend/vulkan/o_b_vulkan_loader.h"

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
} SwapchainDesc;

// The caller must wait for its Vulkan work on the swapchain images before resizing or destroying it.
Swapchain *swapchain_create(void *native_window_handle, SwapchainDesc desc);
bool swapchain_resize(Swapchain *sc, uint32_t width, uint32_t height);
void swapchain_destroy(Swapchain *sc);

VkExtent2D swapchain_extent(const Swapchain *sc);
bool swapchain_wait_for_frame(Swapchain *sc, uint32_t timeoutMs);
SwapchainFrameSync swapchain_frame_sync(const Swapchain *sc);
bool swapchain_is_lost(const Swapchain *sc);
SwapchainPresentResult swapchain_present(Swapchain *sc, bool vsync);

#endif /* O_B_P_SWAPCHAIN_H */

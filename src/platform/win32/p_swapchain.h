#ifndef SWAPCHAIN_H
#define SWAPCHAIN_H

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct Swapchain Swapchain;
typedef struct OWindow OWindow;

typedef enum SwapchainAcquireResult {
    SWAPCHAIN_ACQUIRE_SUCCESS = 0,
    SWAPCHAIN_ACQUIRE_OUT_OF_DATE,
    SWAPCHAIN_ACQUIRE_FAILED,
} SwapchainAcquireResult;

typedef enum SwapchainPresentResult {
    SWAPCHAIN_PRESENT_SUCCESS = 0,
    SWAPCHAIN_PRESENT_OUT_OF_DATE,
    SWAPCHAIN_PRESENT_FAILED,
} SwapchainPresentResult;

typedef struct SwapchainDesc {
    VkDevice device;
    VkPhysicalDevice physDevice;
    VkInstance instance;
    bool vsync;
} SwapchainDesc;

Swapchain *swapchain_create(void *native_window_handle, SwapchainDesc desc);
bool swapchain_resize(Swapchain *sc, uint32_t width, uint32_t height);
void swapchain_destroy(Swapchain *sc);

SwapchainAcquireResult swapchain_acquire(Swapchain *sc, uint32_t * imageIndex);
SwapchainPresentResult swapchain_present(Swapchain *sc, uint32_t imageIndex);

#endif /* SWAPCHAIN_H */

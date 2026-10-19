#include "p_vulkan_loader.h"

#include "o_log.h"

/* Global */
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;

/* Instance */
PFN_vkCreateInstance vkCreateInstance;
PFN_vkDestroyInstance vkDestroyInstance;
PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2;
PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;

PFN_vkCreateDevice vkCreateDevice;

PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;

PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;

/* Device */
PFN_vkDestroyDevice vkDestroyDevice;
PFN_vkGetDeviceQueue vkGetDeviceQueue;
PFN_vkDeviceWaitIdle vkDeviceWaitIdle;

PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;

PFN_vkQueueSubmit vkQueueSubmit;
PFN_vkQueuePresentKHR vkQueuePresentKHR;
PFN_vkQueueWaitIdle vkQueueWaitIdle;

PFN_vkCreateSemaphore vkCreateSemaphore;
PFN_vkDestroySemaphore vkDestroySemaphore;
PFN_vkCreateFence vkCreateFence;
PFN_vkDestroyFence vkDestroyFence;
PFN_vkWaitForFences vkWaitForFences;
PFN_vkResetFences vkResetFences;

PFN_vkCreateCommandPool vkCreateCommandPool;
PFN_vkDestroyCommandPool vkDestroyCommandPool;
PFN_vkResetCommandPool vkResetCommandPool;
PFN_vkResetCommandBuffer vkResetCommandBuffer;
PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
PFN_vkFreeCommandBuffers vkFreeCommandBuffers;

PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
PFN_vkEndCommandBuffer vkEndCommandBuffer;
PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
PFN_vkCmdClearColorImage vkCmdClearColorImage;

/* Buffer management */
PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
PFN_vkCreateBuffer vkCreateBuffer;
PFN_vkDestroyBuffer vkDestroyBuffer;
PFN_vkCreateImage vkCreateImage;
PFN_vkDestroyImage vkDestroyImage;
PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
PFN_vkAllocateMemory vkAllocateMemory;
PFN_vkFreeMemory vkFreeMemory;
PFN_vkBindBufferMemory vkBindBufferMemory;
PFN_vkBindImageMemory vkBindImageMemory;
PFN_vkMapMemory vkMapMemory;
PFN_vkUnmapMemory vkUnmapMemory;
PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;
PFN_vkCmdCopyBuffer vkCmdCopyBuffer;
PFN_vkCmdCopyImage vkCmdCopyImage;
PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;

/* Shader management */
PFN_vkCreateShaderModule vkCreateShaderModule;
PFN_vkDestroyShaderModule vkDestroyShaderModule;

/* Render pass */
PFN_vkCreateRenderPass vkCreateRenderPass;
PFN_vkDestroyRenderPass vkDestroyRenderPass;

/* Image views */
PFN_vkCreateImageView vkCreateImageView;
PFN_vkDestroyImageView vkDestroyImageView;

/* Framebuffers */
PFN_vkCreateFramebuffer vkCreateFramebuffer;
PFN_vkDestroyFramebuffer vkDestroyFramebuffer;

/* Pipeline */
PFN_vkCreatePipelineLayout vkCreatePipelineLayout;
PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout;
PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines;
PFN_vkDestroyPipeline vkDestroyPipeline;

/* Render pass commands */
PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
PFN_vkCmdBindPipeline vkCmdBindPipeline;
PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers;
PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer;
PFN_vkCmdSetViewport vkCmdSetViewport;
PFN_vkCmdSetScissor vkCmdSetScissor;
PFN_vkCmdDraw vkCmdDraw;
PFN_vkCmdDrawIndexed vkCmdDrawIndexed;
PFN_vkCmdPushConstants vkCmdPushConstants;

static void *vk_get_global(const char *name) {
    if (!vkGetInstanceProcAddr) {
        return NULL;
    }
    return (void *)vkGetInstanceProcAddr(VK_NULL_HANDLE, name);
}

void *vk_get_instance(VkInstance instance, const char *name) {
    if (!vkGetInstanceProcAddr) {
        return NULL;
    }
    return (void *)vkGetInstanceProcAddr(instance, name);
}

void *vk_get_device(VkDevice device, const char *name) {
    if (!vkGetDeviceProcAddr) {
        return NULL;
    }
    return (void *)vkGetDeviceProcAddr(device, name);
}

bool vk_load_instance_fns(VkInstance instance) {
    vkDestroyInstance = (PFN_vkDestroyInstance)vk_get_instance(instance, "vkDestroyInstance");
    vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)vk_get_instance(instance, "vkEnumeratePhysicalDevices");
    vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)vk_get_instance(instance, "vkGetPhysicalDeviceProperties");
    vkGetPhysicalDeviceProperties2 = (PFN_vkGetPhysicalDeviceProperties2)vk_get_instance(instance, "vkGetPhysicalDeviceProperties2");
    vkEnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)vk_get_instance(instance, "vkEnumerateDeviceExtensionProperties");
    vkGetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vk_get_instance(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    vkCreateDevice = (PFN_vkCreateDevice)vk_get_instance(instance, "vkCreateDevice");
    vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)vk_get_instance(instance, "vkGetPhysicalDeviceMemoryProperties");

    vkCreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)vk_get_instance(instance, "vkCreateWin32SurfaceKHR");
    vkDestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)vk_get_instance(instance, "vkDestroySurfaceKHR");

    vkGetPhysicalDeviceSurfaceSupportKHR = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)vk_get_instance(instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)vk_get_instance(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    vkGetPhysicalDeviceSurfaceFormatsKHR = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)vk_get_instance(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    vkGetPhysicalDeviceSurfacePresentModesKHR = (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)vk_get_instance(instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");

    return vkDestroyInstance && vkEnumeratePhysicalDevices && vkGetPhysicalDeviceQueueFamilyProperties && vkCreateDevice &&
           vkCreateWin32SurfaceKHR && vkDestroySurfaceKHR && vkGetPhysicalDeviceSurfaceSupportKHR &&
           vkGetPhysicalDeviceSurfaceCapabilitiesKHR && vkGetPhysicalDeviceSurfaceFormatsKHR && vkGetPhysicalDeviceSurfacePresentModesKHR &&
           vkGetPhysicalDeviceMemoryProperties;
}

bool vk_load_device_fns(VkDevice device) {
    vkDestroyDevice = (PFN_vkDestroyDevice)vk_get_device(device, "vkDestroyDevice");
    vkGetDeviceQueue = (PFN_vkGetDeviceQueue)vk_get_device(device, "vkGetDeviceQueue");
    vkDeviceWaitIdle = (PFN_vkDeviceWaitIdle)vk_get_device(device, "vkDeviceWaitIdle");

    vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)vk_get_device(device, "vkCreateSwapchainKHR");
    vkDestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)vk_get_device(device, "vkDestroySwapchainKHR");
    vkGetSwapchainImagesKHR = (PFN_vkGetSwapchainImagesKHR)vk_get_device(device, "vkGetSwapchainImagesKHR");
    vkAcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)vk_get_device(device, "vkAcquireNextImageKHR");

    vkQueueSubmit = (PFN_vkQueueSubmit)vk_get_device(device, "vkQueueSubmit");
    vkQueuePresentKHR = (PFN_vkQueuePresentKHR)vk_get_device(device, "vkQueuePresentKHR");
    vkQueueWaitIdle = (PFN_vkQueueWaitIdle)vk_get_device(device, "vkQueueWaitIdle");

    vkCreateSemaphore = (PFN_vkCreateSemaphore)vk_get_device(device, "vkCreateSemaphore");
    vkDestroySemaphore = (PFN_vkDestroySemaphore)vk_get_device(device, "vkDestroySemaphore");
    vkCreateFence = (PFN_vkCreateFence)vk_get_device(device, "vkCreateFence");
    vkDestroyFence = (PFN_vkDestroyFence)vk_get_device(device, "vkDestroyFence");
    vkWaitForFences = (PFN_vkWaitForFences)vk_get_device(device, "vkWaitForFences");
    vkResetFences = (PFN_vkResetFences)vk_get_device(device, "vkResetFences");

    vkCreateCommandPool = (PFN_vkCreateCommandPool)vk_get_device(device, "vkCreateCommandPool");
    vkDestroyCommandPool = (PFN_vkDestroyCommandPool)vk_get_device(device, "vkDestroyCommandPool");
    vkResetCommandPool = (PFN_vkResetCommandPool)vk_get_device(device, "vkResetCommandPool");
    vkResetCommandBuffer = (PFN_vkResetCommandBuffer)vk_get_device(device, "vkResetCommandBuffer");
    vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)vk_get_device(device, "vkAllocateCommandBuffers");
    vkFreeCommandBuffers = (PFN_vkFreeCommandBuffers)vk_get_device(device, "vkFreeCommandBuffers");

    vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)vk_get_device(device, "vkBeginCommandBuffer");
    vkEndCommandBuffer = (PFN_vkEndCommandBuffer)vk_get_device(device, "vkEndCommandBuffer");
    vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)vk_get_device(device, "vkCmdPipelineBarrier");
    vkCmdClearColorImage = (PFN_vkCmdClearColorImage)vk_get_device(device, "vkCmdClearColorImage");

    /* Buffer management functions */
    vkCreateBuffer = (PFN_vkCreateBuffer)vk_get_device(device, "vkCreateBuffer");
    vkDestroyBuffer = (PFN_vkDestroyBuffer)vk_get_device(device, "vkDestroyBuffer");
    vkCreateImage = (PFN_vkCreateImage)vk_get_device(device, "vkCreateImage");
    vkDestroyImage = (PFN_vkDestroyImage)vk_get_device(device, "vkDestroyImage");
    vkGetBufferMemoryRequirements = (PFN_vkGetBufferMemoryRequirements)vk_get_device(device, "vkGetBufferMemoryRequirements");
    vkGetImageMemoryRequirements = (PFN_vkGetImageMemoryRequirements)vk_get_device(device, "vkGetImageMemoryRequirements");
    vkAllocateMemory = (PFN_vkAllocateMemory)vk_get_device(device, "vkAllocateMemory");
    vkFreeMemory = (PFN_vkFreeMemory)vk_get_device(device, "vkFreeMemory");
    vkBindBufferMemory = (PFN_vkBindBufferMemory)vk_get_device(device, "vkBindBufferMemory");
    vkBindImageMemory = (PFN_vkBindImageMemory)vk_get_device(device, "vkBindImageMemory");
    vkMapMemory = (PFN_vkMapMemory)vk_get_device(device, "vkMapMemory");
    vkUnmapMemory = (PFN_vkUnmapMemory)vk_get_device(device, "vkUnmapMemory");
    vkFlushMappedMemoryRanges = (PFN_vkFlushMappedMemoryRanges)vk_get_device(device, "vkFlushMappedMemoryRanges");
    vkCmdCopyBuffer = (PFN_vkCmdCopyBuffer)vk_get_device(device, "vkCmdCopyBuffer");
    vkCmdCopyImage = (PFN_vkCmdCopyImage)vk_get_device(device, "vkCmdCopyImage");
    vkCmdCopyBufferToImage = (PFN_vkCmdCopyBufferToImage)vk_get_device(device, "vkCmdCopyBufferToImage");

    /* Shader management functions */
    vkCreateShaderModule = (PFN_vkCreateShaderModule)vk_get_device(device, "vkCreateShaderModule");
    vkDestroyShaderModule = (PFN_vkDestroyShaderModule)vk_get_device(device, "vkDestroyShaderModule");

    /* Render pass */
    vkCreateRenderPass = (PFN_vkCreateRenderPass)vk_get_device(device, "vkCreateRenderPass");
    vkDestroyRenderPass = (PFN_vkDestroyRenderPass)vk_get_device(device, "vkDestroyRenderPass");

    /* Image views */
    vkCreateImageView = (PFN_vkCreateImageView)vk_get_device(device, "vkCreateImageView");
    vkDestroyImageView = (PFN_vkDestroyImageView)vk_get_device(device, "vkDestroyImageView");

    /* Framebuffers */
    vkCreateFramebuffer = (PFN_vkCreateFramebuffer)vk_get_device(device, "vkCreateFramebuffer");
    vkDestroyFramebuffer = (PFN_vkDestroyFramebuffer)vk_get_device(device, "vkDestroyFramebuffer");

    /* Pipeline */
    vkCreatePipelineLayout = (PFN_vkCreatePipelineLayout)vk_get_device(device, "vkCreatePipelineLayout");
    vkDestroyPipelineLayout = (PFN_vkDestroyPipelineLayout)vk_get_device(device, "vkDestroyPipelineLayout");
    vkCreateGraphicsPipelines = (PFN_vkCreateGraphicsPipelines)vk_get_device(device, "vkCreateGraphicsPipelines");
    vkDestroyPipeline = (PFN_vkDestroyPipeline)vk_get_device(device, "vkDestroyPipeline");

    /* Render pass commands */
    vkCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)vk_get_device(device, "vkCmdBeginRenderPass");
    vkCmdEndRenderPass = (PFN_vkCmdEndRenderPass)vk_get_device(device, "vkCmdEndRenderPass");
    vkCmdBindPipeline = (PFN_vkCmdBindPipeline)vk_get_device(device, "vkCmdBindPipeline");
    vkCmdBindVertexBuffers = (PFN_vkCmdBindVertexBuffers)vk_get_device(device, "vkCmdBindVertexBuffers");
    vkCmdBindIndexBuffer = (PFN_vkCmdBindIndexBuffer)vk_get_device(device, "vkCmdBindIndexBuffer");
    vkCmdSetViewport = (PFN_vkCmdSetViewport)vk_get_device(device, "vkCmdSetViewport");
    vkCmdSetScissor = (PFN_vkCmdSetScissor)vk_get_device(device, "vkCmdSetScissor");
    vkCmdDraw = (PFN_vkCmdDraw)vk_get_device(device, "vkCmdDraw");
    vkCmdDrawIndexed = (PFN_vkCmdDrawIndexed)vk_get_device(device, "vkCmdDrawIndexed");
    vkCmdPushConstants = (PFN_vkCmdPushConstants)vk_get_device(device, "vkCmdPushConstants");

    return vkDestroyDevice && vkGetDeviceQueue && vkCreateSwapchainKHR && vkGetSwapchainImagesKHR &&
           vkAcquireNextImageKHR && vkQueueSubmit && vkQueuePresentKHR && vkCreateSemaphore && vkCreateFence &&
           vkWaitForFences && vkResetFences && vkCreateCommandPool && vkResetCommandBuffer && vkAllocateCommandBuffers &&
           vkBeginCommandBuffer && vkEndCommandBuffer && vkCmdPipelineBarrier && vkCmdClearColorImage &&
           vkCreateBuffer && vkDestroyBuffer && vkGetBufferMemoryRequirements && vkAllocateMemory && vkFreeMemory && vkBindBufferMemory &&
           vkMapMemory && vkUnmapMemory && vkFlushMappedMemoryRanges && vkCmdCopyBuffer &&
           vkCreateShaderModule && vkDestroyShaderModule &&
           vkCreateRenderPass && vkDestroyRenderPass &&
           vkCreateImageView && vkDestroyImageView &&
           vkCreateFramebuffer && vkDestroyFramebuffer &&
           vkCreatePipelineLayout && vkDestroyPipelineLayout &&
           vkCreateGraphicsPipelines && vkDestroyPipeline &&
           vkCmdBeginRenderPass && vkCmdEndRenderPass &&
           vkCmdBindPipeline && vkCmdBindVertexBuffers && vkCmdBindIndexBuffer &&
           vkCmdSetViewport && vkCmdSetScissor &&
           vkCmdDraw && vkCmdDrawIndexed && vkCmdPushConstants;
}

bool vk_load_lib(HMODULE* vulkan_lib_handle) {
    *vulkan_lib_handle = LoadLibraryA("vulkan-1.dll");
    if (!*vulkan_lib_handle) {
        LOG_ERROR("failed to load vulkan-1.dll (Vulkan runtime not installed?)");
        return false;
    }

    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(*vulkan_lib_handle, "vkGetInstanceProcAddr");
    vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)GetProcAddress(*vulkan_lib_handle, "vkGetDeviceProcAddr");

    if (!vkGetInstanceProcAddr || !vkGetDeviceProcAddr) {
        LOG_ERROR("failed to get Vulkan proc address functions");
        return false;
    }

    vkCreateInstance = (PFN_vkCreateInstance)vk_get_global("vkCreateInstance");
    vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)vk_get_global("vkEnumerateInstanceExtensionProperties");
    vkEnumerateInstanceLayerProperties = (PFN_vkEnumerateInstanceLayerProperties)vk_get_global("vkEnumerateInstanceLayerProperties");

    if (!vkCreateInstance || !vkEnumerateInstanceExtensionProperties) {
        LOG_ERROR("failed to load required Vulkan loader entry points");
        return false;
    }

    return true;
}

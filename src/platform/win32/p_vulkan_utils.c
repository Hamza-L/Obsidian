#include "p_vulkan_utils.h"
#include "p_vulkan_loader.h"

#include "o_log.h"
#include "o_window.h"

#include "vulkan/vulkan_core.h"


#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>

struct VkContext{
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physDevice;
    struct {
        VkQueue graphics;
        VkQueue transfer;
        uint32_t graphicsqfi;
        uint32_t transferqfi;
    } queue;

    bool isInit;
    HMODULE vulkanLib;
} ctx;

bool vk_test_surface_present_support(OWindow * window) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    HWND hwnd = (HWND)window_native_handle(window);
    if (!hwnd) {
        LOG_ERROR("vk_test_surface_present_support: cannot retrieve window hwnd");
        return false;
    }

    VkWin32SurfaceCreateInfoKHR sci;
    memset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    sci.hinstance = GetModuleHandleW(NULL);
    sci.hwnd = hwnd;

    VkResult res = vkCreateWin32SurfaceKHR(ctx.instance, &sci, NULL, &surface);
    if (res != VK_SUCCESS) {
        LOG_ERROR("Failed to create surface\n");
        return false;
    }

    VkBool32 supportsPresent = VK_FALSE;
    res = vkGetPhysicalDeviceSurfaceSupportKHR(ctx.physDevice, ctx.queue.graphicsqfi, surface, &supportsPresent);
    if (res != VK_SUCCESS || supportsPresent) {
        return false;
    }

    return true;
}

VkContext *vk_context_create(VkContextDesc desc) {
    VkExtensionProperties *availExts = NULL;
    uint32_t nAvailExts = 0;
    VkLayerProperties *availLayers = NULL;
    uint32_t nAvailLayers = 0;
    VkContext *result = NULL;

    uint32_t nReqInstanceExts = 0;
    while (nReqInstanceExts < MAX_REQUESTED_INSTANCE_EXT && desc.requiredInstanceExts[nReqInstanceExts]) { nReqInstanceExts++; }
    uint32_t nReqDeviceExts = 0;
    while (nReqDeviceExts < MAX_REQUESTED_DEVICE_EXT && desc.requiredDeviceExts[nReqDeviceExts]) { nReqDeviceExts++;}

    // load the vulkan dll
    if (!ctx.vulkanLib) {
        if (!vk_load_lib(&ctx.vulkanLib)) {
            LOG_ERROR("vk_context_create(): Failed to load vulkan lib");
            goto done;
        } else {
            ctx.isInit = true;
        }
    }

    /* Enumerate extensions and layers once, then check against the cached lists. */
    vkEnumerateInstanceExtensionProperties(NULL, &nAvailExts, NULL);
    if (nAvailExts > 0) {
        availExts = (VkExtensionProperties *)calloc(nAvailExts, sizeof(VkExtensionProperties));
        if (!availExts){
            LOG_ERROR("vk_context_create(): failed to acquire the available exts\n");
            goto done;
        }
        vkEnumerateInstanceExtensionProperties(NULL, &nAvailExts, availExts);
    }

    vkEnumerateInstanceLayerProperties(&nAvailLayers, NULL);
    if (nAvailLayers > 0) {
        availLayers = (VkLayerProperties *)calloc(nAvailLayers, sizeof(VkLayerProperties));
        if (availLayers) {
            vkEnumerateInstanceLayerProperties(&nAvailLayers, availLayers);
        }
    }

    const char *extensions[32];
    uint32_t nExt = 0;
    for (uint32_t i = 0; i < nReqInstanceExts; i++) {
        bool found = false;
        for (uint32_t j = 0; j < nAvailExts; j++) {
            if (strcmp(availExts[j].extensionName, desc.requiredInstanceExts[i]) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            LOG_ERROR("vk_context_create: required Vulkan instance extension not available: %s", desc.requiredInstanceExts[i]);
            goto done;
        }
        extensions[nExt++] = desc.requiredInstanceExts[i];
    }

    /* Best-effort validation support. */
    const char *layers[4];
    uint32_t nLayers = 0;
    if (desc.enableValidationLayer) {
        const char *val_layer = "VK_LAYER_KHRONOS_validation";
        bool has_val_layer = false;
        for (uint32_t i = 0; i < nAvailLayers && availLayers; i++) {
            if (strcmp(availLayers[i].layerName, val_layer) == 0) {
                has_val_layer = true;
                break;
            }
        }
        if (has_val_layer) {
            layers[nLayers++] = val_layer;

            /* Debug utils is optional; we wire it up later. */
            for (uint32_t i = 0; i < nAvailExts && availExts; i++) {
                if (strcmp(availExts[i].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
                    extensions[nExt++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
                    break;
                }
            }
        } else {
            LOG_WARN("validation requested but VK_LAYER_KHRONOS_validation not available");
        }
    }

    VkApplicationInfo appInfo;
    memset(&appInfo, 0, sizeof(appInfo));
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Obsidian";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    appInfo.pEngineName = "Obsidian";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 0, 1);
    appInfo.apiVersion = VK_API_VERSION_1_4;

    VkInstanceCreateInfo ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &appInfo;
    ici.enabledExtensionCount = nExt;
    ici.ppEnabledExtensionNames = extensions;
    ici.enabledLayerCount = nLayers;
    ici.ppEnabledLayerNames = nLayers ? layers : NULL;

    VkResult res = vkCreateInstance(&ici, NULL, &ctx.instance);
    if (res != VK_SUCCESS || ctx.instance == VK_NULL_HANDLE) {
        LOG_ERROR("vk_context_create: vkCreateInstance failed: VkResult %d", (int)res);
        goto done;
    }

    if (!vk_load_instance_fns(ctx.instance)) {
        LOG_ERROR("vk_context_create: failed to load vulkan functions ");
        goto done;
    }

    uint32_t npd = 0;
    res = vkEnumeratePhysicalDevices(ctx.instance, &npd, NULL);
    if (res != VK_SUCCESS || npd == 0) {
        LOG_ERROR("vkEnumeratePhysicalDevices failed or no devices (vr=%d)", (int)res);
        goto done;
    }

    VkPhysicalDevice physDevices[MAX_PHYS_DEVICES];
    res = vkEnumeratePhysicalDevices(ctx.instance, &npd, physDevices);
    if (res != VK_SUCCESS) {
        LOG_ERROR("vkEnumeratePhysicalDevices failed or no devices (vr=%d)", (int)res);
        goto done;
    }

    uint32_t graphicsqfi = UINT32_MAX;
    uint32_t transferqfi = UINT32_MAX;
    VkPhysicalDevice physd = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < npd && i < MAX_PHYS_DEVICES; i++) {
        uint32_t nqfi = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physDevices[i], &nqfi, NULL);
        if (nqfi == 0) continue;

        VkQueueFamilyProperties *qfs = (VkQueueFamilyProperties *)calloc(nqfi, sizeof(VkQueueFamilyProperties));
        if (!qfs) {
            LOG_ERROR("vk_create_context(): failed to allocate memory\n");
            continue;
        }

        vkGetPhysicalDeviceQueueFamilyProperties(physDevices[i], &nqfi, qfs);

        for (uint32_t j = 0; j < nqfi; j++) {
            if ((qfs[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0){
                graphicsqfi = j;
            }

            if ((qfs[j].queueFlags & VK_QUEUE_TRANSFER_BIT) != 0){
                transferqfi = j;
            }

            if (transferqfi != UINT32_MAX && graphicsqfi != UINT32_MAX) {
                physd = physDevices[i];
                break;
            }
        }

        free(qfs);

        if (physd != VK_NULL_HANDLE) {
            break;
        }
    }

    if (physd == VK_NULL_HANDLE) {
        LOG_ERROR("vk_context_create(): no suitable Vulkan physical device/queue family found (need graphics+present)");
        goto done;
    } else {
        ctx.physDevice = physd;
        ctx.queue.graphicsqfi = graphicsqfi;
        ctx.queue.transferqfi = transferqfi;
    }


    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci[2];
    qci[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci[0].queueFamilyIndex = graphicsqfi;
    qci[0].queueCount = 1;
    qci[0].pQueuePriorities = &prio;
    qci[0].pNext = NULL;
    qci[0].flags = 0;

    qci[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci[1].queueFamilyIndex = transferqfi;
    qci[1].queueCount = 1;
    qci[1].pQueuePriorities = &prio;
    qci[1].pNext = NULL;
    qci[1].flags = 0;

    VkDeviceCreateInfo dci;
    memset(&dci, 0, sizeof(dci));
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = (graphicsqfi == transferqfi) ? 1 : 2;
    dci.pQueueCreateInfos = qci;
    dci.enabledExtensionCount = nReqDeviceExts;
    dci.ppEnabledExtensionNames = desc.requiredDeviceExts;

    res = vkCreateDevice(ctx.physDevice, &dci, NULL, &ctx.device);
    if (res != VK_SUCCESS || ctx.device == VK_NULL_HANDLE) {
        LOG_ERROR("vkCreateDevice failed (vr=%d)", (int)res);
        goto done;
    }

    if (!vk_load_device_fns(ctx.device)) {
        LOG_ERROR("failed to load Vulkan device functions");
        goto done;
    }

    vkGetDeviceQueue(ctx.device, graphicsqfi, 0, &ctx.queue.graphics);
    if (ctx.queue.graphics == VK_NULL_HANDLE) {
        LOG_ERROR("vkGetDeviceQueue returned NULL queue");
        goto done;
    }

    if (transferqfi != graphicsqfi) {
        vkGetDeviceQueue(ctx.device, transferqfi, 0, &ctx.queue.transfer);
        if (ctx.queue.transfer == VK_NULL_HANDLE) {
            LOG_ERROR("vkGetDeviceQueue returned NULL queue");
            goto done;
        }
    } else {
        ctx.queue.transfer = ctx.queue.graphics;
    }

    result = &ctx;

done:
    free(availExts);
    free(availLayers);
    if (!result) {
        vk_context_destroy(&ctx);
    }
    return result;
}

void vk_context_destroy(VkContext *ctx) {
    if (!ctx) return;

    if (ctx->device && vkDestroyDevice) vkDestroyDevice(ctx->device, NULL);
    if (ctx->instance && vkDestroyInstance) vkDestroyInstance(ctx->instance, NULL);
    if (ctx->vulkanLib) FreeLibrary(ctx->vulkanLib);

    memset(ctx, 0, sizeof(*ctx));
}

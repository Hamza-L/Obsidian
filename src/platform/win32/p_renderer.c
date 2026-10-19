#include "o_renderer.h"
#include "o_log.h"
#include "o_window.h"

#include "p_vulkan_loader.h"
#include "p_vulkan_limits.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>

static const char *const k_instance_extensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
};

static const char *const k_device_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
    // VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
    // VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME,
    // VK_KHR_DEVICE_ADDRESS_COMMANDS_EXTENSION_NAME,
    // VK_KHR_UNIFIED_IMAGE_LAYOUTS_EXTENSION_NAME
};

#define COUNT_OF(array) ((uint32_t)(sizeof(array) / sizeof((array)[0])))

struct ORenderer {
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physDevice;
    struct {
        VkQueue graphics;
        VkQueue transfer;
        uint32_t graphicsqfi;
        uint32_t transferqfi;
    } queue;

    HMODULE vulkanLib;
} g_renderer;

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

    VkResult res = vkCreateWin32SurfaceKHR(g_renderer.instance, &sci, NULL, &surface);
    if (res != VK_SUCCESS) {
        LOG_ERROR("Failed to create surface\n");
        return false;
    }

    VkBool32 supportsPresent = VK_FALSE;
    res = vkGetPhysicalDeviceSurfaceSupportKHR(g_renderer.physDevice, g_renderer.queue.graphicsqfi, surface, &supportsPresent);
    vkDestroySurfaceKHR(g_renderer.instance, surface, NULL);

    return res == VK_SUCCESS && supportsPresent == VK_TRUE;
}

static bool device_has_extensions(VkPhysicalDevice device, const char *const *names, uint32_t count) {
    if (count == 0) return true;
    uint32_t available = 0;
    if (vkEnumerateDeviceExtensionProperties(device, NULL, &available, NULL) != VK_SUCCESS) return false;
    VkExtensionProperties *props = calloc(available, sizeof *props);
    if (!props) return false;
    vkEnumerateDeviceExtensionProperties(device, NULL, &available, props);

    bool all_found = true;
    for (uint32_t i = 0; i < count && all_found; i++) {
        bool found = false;
        for (uint32_t j = 0; j < available && !found; j++) found = strcmp(props[j].extensionName, names[i]) == 0;
        all_found = found;
    }
    free(props);
    return all_found;
}

static bool instance_has_extensions(const char *const *names, uint32_t count) {
    if (count == 0) return true;
    uint32_t available = 0;
    if (vkEnumerateInstanceExtensionProperties(NULL, &available, NULL) != VK_SUCCESS) return false;
    VkExtensionProperties *props = calloc(available, sizeof *props);
    if (!props) return false;
    vkEnumerateInstanceExtensionProperties(NULL, &available, props);

    bool all_found = true;
    for (uint32_t i = 0; i < count && all_found; i++) {
        bool found = false;
        for (uint32_t j = 0; j < available && !found; j++) found = strcmp(props[j].extensionName, names[i]) == 0;
        all_found = found;
        if (!found) LOG_ERROR("required Vulkan instance extension not available: %s", names[i]);
    }
    free(props);
    return all_found;
}

ORenderer *renderer_create(ORendererDesc *desc) {
    (void)desc;
    VkLayerProperties *availLayers = NULL;
    uint32_t nAvailLayers = 0;

    const uint32_t nReqInstanceExts = COUNT_OF(k_instance_extensions);
    const uint32_t nReqDeviceExts = COUNT_OF(k_device_extensions);

    // load the vulkan dll
    if (!g_renderer.vulkanLib) {
        if (!vk_load_lib(&g_renderer.vulkanLib)) {
            LOG_ERROR("renderer_create(): Failed to load vulkan lib");
            renderer_destroy(&g_renderer);
            return NULL;
        }
    }

    if (!instance_has_extensions(k_instance_extensions, nReqInstanceExts)) {
        renderer_destroy(&g_renderer);
        return NULL;
    }

    const char *extensions[32];
    uint32_t nExt = 0;
    for (uint32_t i = 0; i < nReqInstanceExts; i++) extensions[nExt++] = k_instance_extensions[i];

    vkEnumerateInstanceLayerProperties(&nAvailLayers, NULL);
    if (nAvailLayers > 0) {
        availLayers = (VkLayerProperties *)calloc(nAvailLayers, sizeof(VkLayerProperties));
        if (availLayers) {
            vkEnumerateInstanceLayerProperties(&nAvailLayers, availLayers);
        }
    }

    /* Best-effort validation support. */
    const char *layers[4];
    uint32_t nLayers = 0;
    if (RENDERER_VALIDATION) {
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

            const char *debug_utils = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
            if (instance_has_extensions(&debug_utils, 1)) extensions[nExt++] = debug_utils;
        } else {
            LOG_WARN("validation requested but VK_LAYER_KHRONOS_validation not available");
        }
    }
    free(availLayers);

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

    VkResult res = vkCreateInstance(&ici, NULL, &g_renderer.instance);
    if (res != VK_SUCCESS || g_renderer.instance == VK_NULL_HANDLE) {
        LOG_ERROR("renderer_create: vkCreateInstance failed: VkResult %d", (int)res);
        renderer_destroy(&g_renderer);
        return NULL;
    }

    if (!vk_load_instance_fns(g_renderer.instance)) {
        LOG_ERROR("renderer_create: failed to load vulkan functions ");
        renderer_destroy(&g_renderer);
        return NULL;
    }

    uint32_t npd = 0;
    res = vkEnumeratePhysicalDevices(g_renderer.instance, &npd, NULL);
    if (res != VK_SUCCESS || npd == 0) {
        LOG_ERROR("vkEnumeratePhysicalDevices failed or no devices (vr=%d)", (int)res);
        renderer_destroy(&g_renderer);
        return NULL;
    }

    VkPhysicalDevice physDevices[MAX_PHYS_DEVICES];
    if (npd > MAX_PHYS_DEVICES) npd = MAX_PHYS_DEVICES;
    res = vkEnumeratePhysicalDevices(g_renderer.instance, &npd, physDevices);
    if (res != VK_SUCCESS && res != VK_INCOMPLETE) {
        LOG_ERROR("vkEnumeratePhysicalDevices failed or no devices (vr=%d)", (int)res);
        renderer_destroy(&g_renderer);
        return NULL;
    }

    uint32_t graphicsqfi = UINT32_MAX;
    uint32_t transferqfi = UINT32_MAX;
    VkPhysicalDevice physd = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < npd && i < MAX_PHYS_DEVICES; i++) {
        if (!device_has_extensions(physDevices[i], k_device_extensions, nReqDeviceExts)) continue;
        graphicsqfi = UINT32_MAX;
        transferqfi = UINT32_MAX;
        uint32_t nqfi = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physDevices[i], &nqfi, NULL);
        if (nqfi == 0) continue;

        VkQueueFamilyProperties *qfs = (VkQueueFamilyProperties *)calloc(nqfi, sizeof(VkQueueFamilyProperties));
        if (!qfs) {
            LOG_ERROR("renderer_create(): failed to allocate memory\n");
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
        LOG_ERROR("renderer_create(): no suitable Vulkan physical device/queue family found (need graphics+present)");
        renderer_destroy(&g_renderer);
        return NULL;
    } else {
        g_renderer.physDevice = physd;
        g_renderer.queue.graphicsqfi = graphicsqfi;
        g_renderer.queue.transferqfi = transferqfi;
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
    dci.ppEnabledExtensionNames = k_device_extensions;

    res = vkCreateDevice(g_renderer.physDevice, &dci, NULL, &g_renderer.device);
    if (res != VK_SUCCESS || g_renderer.device == VK_NULL_HANDLE) {
        LOG_ERROR("vkCreateDevice failed (vr=%d)", (int)res);
        renderer_destroy(&g_renderer);
        return NULL;
    }

    if (!vk_load_device_fns(g_renderer.device)) {
        LOG_ERROR("failed to load Vulkan device functions");
        renderer_destroy(&g_renderer);
        return NULL;
    }

    vkGetDeviceQueue(g_renderer.device, graphicsqfi, 0, &g_renderer.queue.graphics);
    if (g_renderer.queue.graphics == VK_NULL_HANDLE) {
        LOG_ERROR("vkGetDeviceQueue returned NULL queue");
        renderer_destroy(&g_renderer);
        return NULL;
    }

    if (transferqfi != graphicsqfi) {
        vkGetDeviceQueue(g_renderer.device, transferqfi, 0, &g_renderer.queue.transfer);
        if (g_renderer.queue.transfer == VK_NULL_HANDLE) {
            LOG_ERROR("vkGetDeviceQueue returned NULL queue");
            renderer_destroy(&g_renderer);
            return NULL;
        }
    } else {
        g_renderer.queue.transfer = g_renderer.queue.graphics;
    }

    return &g_renderer;
}

void renderer_destroy(ORenderer *renderer) {
    if (!renderer) return;

    if (renderer->device) vkDestroyDevice(renderer->device, NULL);
    if (renderer->instance) vkDestroyInstance(renderer->instance, NULL);
    if (renderer->vulkanLib) FreeLibrary(renderer->vulkanLib);

    memset(renderer, 0, sizeof(*renderer));
}

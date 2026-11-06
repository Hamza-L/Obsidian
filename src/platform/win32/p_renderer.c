#include "o_renderer.h"
#include "o_log.h"
#include "o_window.h"
#include "o_memory.h"

#include "p_vulkan_loader.h"
#include "p_vulkan_limits.h"

#include "p_renderer_internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>

#define COUNT_OF(array) ((uint32_t)(sizeof(array) / sizeof((array)[0])))

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

ORenderer g_renderer;

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

static bool create_fences(VkDevice device, VkFence *fences, size_t count) {
    VkFenceCreateInfo fci = {0};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < count; i++) {
        VkResult vr = vkCreateFence(device, &fci, NULL, &fences[i]);
        if (vr != VK_SUCCESS) {
            LOG_ERROR("vkCreateFence[%u] failed (vr=%d)", (unsigned)i, (int)vr);
            return false;
        }
    }
    return true;
}

static bool create_command_buffers(VkDevice device, VkCommandPool cmdPool, VkCommandBuffer cmdBuffers[], size_t count) {
    VkCommandBufferAllocateInfo cbai;
    memset(&cbai, 0, sizeof(cbai));
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = count;

    VkResult vr = vkAllocateCommandBuffers(device, &cbai, cmdBuffers);
    if (vr != VK_SUCCESS) {
        LOG_ERROR("vkAllocateCommandBuffers failed (vr=%d)", (int)vr);
        return false;
    }

    return true;
}

static bool create_command_pool(VkDevice device, uint32_t queueFamilyIndex, VkCommandPool* cmdPool) {
    VkCommandPoolCreateInfo cpci;
    memset(&cpci, 0, sizeof(cpci));
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = queueFamilyIndex;

    VkResult vr = vkCreateCommandPool(device, &cpci, NULL, cmdPool);
    if (vr != VK_SUCCESS || *cmdPool == VK_NULL_HANDLE) {
        LOG_ERROR("vkCreateCommandPool failed (vr=%d)", (int)vr);
        return false;
    }
    return true;
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

bool create_default_graphics_pipeline(void) {
    return false;
}

ORenderer *renderer_create(ORendererDesc desc) {
    (void)desc;
    memset(&g_renderer, 0, sizeof(g_renderer));

    g_renderer.shaderPool = memory_pool_create(sizeof(OShaderSlot), MAX_SHADER_COUNT);
    g_renderer.imagePool = memory_pool_create(sizeof(OImageSlot), MAX_TEXTURE_COUNT);

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

    if (!create_command_pool(g_renderer.device, g_renderer.queue.graphicsqfi, &g_renderer.cmdPool)) {
        LOG_ERROR("failed to create command pool");
        renderer_destroy(&g_renderer);
        return NULL;
    }

    if (!create_command_buffers(g_renderer.device, g_renderer.cmdPool, g_renderer.cmdBuffer, MAX_FRAMES_IN_FLIGHT)) {
        LOG_ERROR("failed to create command buffers");
        renderer_destroy(&g_renderer);
        return NULL;
    }

    if (!create_fences(g_renderer.device, g_renderer.fences, MAX_FRAMES_IN_FLIGHT)) {
        LOG_ERROR("failed to create in flight fences");
        renderer_destroy(&g_renderer);
        return NULL;
    }

    return &g_renderer;
}

void renderer_destroy(ORenderer *renderer) {
    if (!renderer) return;

    if (renderer->device && vkDeviceWaitIdle) vkDeviceWaitIdle(renderer->device);

    for (uint32_t i = 0; i < MAX_LAYER_COUNT; i++) {
        swapchain_destroy(renderer->layerList.data[i].swapchain);
        renderer->layerList.data[i].swapchain = NULL;
        renderer->layerList.data[i].hwnd = NULL;
    }

    if (renderer->device) {
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (renderer->fences[i]) vkDestroyFence(renderer->device, renderer->fences[i], NULL);
        }
        if (renderer->cmdPool) vkDestroyCommandPool(renderer->device, renderer->cmdPool, NULL);
        if (vkDestroyDevice) vkDestroyDevice(renderer->device, NULL);
    }
    if (renderer->instance && vkDestroyInstance) vkDestroyInstance(renderer->instance, NULL);
    if (renderer->vulkanLib) FreeLibrary(renderer->vulkanLib);

    if (renderer->shaderPool) memory_pool_destroy(renderer->shaderPool);
    if (renderer->imagePool) memory_pool_destroy(renderer->imagePool);

    memset(renderer, 0, sizeof(*renderer));
}


void renderer_wait_idle(ORenderer *renderer) {
    vkDeviceWaitIdle(renderer->device);
}

bool renderer_begin_frame(ORenderer *renderer) {
    (void)renderer;
    return false;
}

void renderer_end_frame_and_present(ORenderer *renderer) {
    (void)renderer;
}

void renderer_begin_render(ORenderer *renderer, ORenderTargets targets) {
    (void)renderer;
    (void)targets;
}

void renderer_end_render(ORenderer *renderer) {
    (void)renderer;
}

OImage renderer_image_create(ORenderer *renderer, OImageDesc desc) {
    (void)renderer;
    (void)desc;
    return (OImage){0};
}

void renderer_image_destroy(ORenderer *renderer, OImage image) {
    (void)renderer;
    (void)image;
}

OSurface renderer_surface_create(ORenderer *renderer, void *native_window_handle, OSurfaceDesc desc) {

    // get the first free layer slot
    int32_t freeID = 0;
    for (int i = 0; i < MAX_LAYER_COUNT; i++) {
        if (renderer->layerList.data[i].hwnd == NULL) {
            freeID = i + 1;
            break;
        }
    }

    // return if all slots are busy
    if (!freeID) {
        LOG_WARN("OUT OF CAPACITY: attempting to create a surface beyond the capacity[8]\n");
        return (OSurface){0};
    }

    uint32_t index = freeID - 1;
    Swapchain *swapchain = swapchain_create(native_window_handle, (SwapchainDesc){.device = g_renderer.device, .physDevice = g_renderer.physDevice, .instance = g_renderer.instance, .vsync = desc.present_mode == O_PRESENT_VSYNC});
    if (!swapchain) {
        LOG_ERROR("renderer_surface_create: failed to create swapchain\n");
        return (OSurface){0};
    }

    renderer->layerList.data[index].hwnd = native_window_handle;
    renderer->layerList.data[index].swapchain = swapchain;
    uint32_t currAge = renderer->layerList.data[index].age;
    return (OSurface){.id = freeID, .age = currAge};
}

void renderer_surface_destroy(ORenderer *renderer, OSurface surface) {
    if (!surface.id || surface.id > MAX_LAYER_COUNT || surface.age != renderer->layerList.data[surface.id - 1].age) {
        LOG_WARN("attempting to destroy an already destroyed surface\n");
        return;
    }

    uint32_t index = surface.id - 1;
    swapchain_destroy(renderer->layerList.data[index].swapchain);
    renderer->layerList.data[index].swapchain = NULL;
    renderer->layerList.data[index].hwnd = NULL;
    renderer->layerList.data[index].age++;
}

void renderer_surface_blit(ORenderer *renderer, OSurface surface, OImage image) {
    (void)renderer;
    (void)surface;
    (void)image;
}

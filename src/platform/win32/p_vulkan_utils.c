#include "p_vulkan_utils.h"
#include "p_vulkan_loader.h"

#include "o_log.h"


#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>

struct {
    bool isInit;
    HMODULE vulkanLib;
} ctx;

void vk_init(void) {
    vk_load_loader(&ctx.vulkanLib);
    if (!ctx.vulkanLib) LOG_ERROR("renderer_create(): Failed to load vulkan lib");
}

void vk_uninit(void) {
    FreeLibrary(ctx.vulkanLib);
}

VkInstance vk_instance_create(VkInstanceDesc desc) {
    VkInstance instance;
    VkExtensionProperties *availExts = NULL;
    uint32_t availExtsCount = 0;
    VkLayerProperties *availLayers = NULL;
    uint32_t avail_layer_count = 0;

    if(!ctx.isInit) vk_init();

    /* Enumerate extensions and layers once, then check against the cached lists. */
    vkEnumerateInstanceExtensionProperties(NULL, &availExtsCount, NULL);
    if (availExtsCount > 0) {
        availExts = (VkExtensionProperties *)calloc(availExtsCount, sizeof(VkExtensionProperties));
        if (!availExts){
            LOG_ERROR("vk_context_create(): failed to acquire the available exts\n");
            goto err_return;
        }
        vkEnumerateInstanceExtensionProperties(NULL, &availExtsCount, availExts);
    }

    vkEnumerateInstanceLayerProperties(&avail_layer_count, NULL);
    if (avail_layer_count > 0) {
        availLayers = (VkLayerProperties *)calloc(avail_layer_count, sizeof(VkLayerProperties));
        if (availLayers) {
            vkEnumerateInstanceLayerProperties(&avail_layer_count, availLayers);
        }
    }

    /* Instance extensions required for Win32 surfaces. */
    const char *extensions[32];
    uint32_t ext_count = 0;
    for (uint32_t i = 0; (i < (uint32_t)sizeof(desc.requiredExts)/sizeof(desc.requiredExts[0])) && desc.requiredExts[i]; i++) {
        // O(N2) : for each required ext, search each available extensions
        bool found = false;
        for (uint32_t j = 0; j < availExtsCount; j++) {
            if (strcmp(availExts[j].extensionName, desc.requiredExts[i]) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            LOG_ERROR("vk_context_create: required Vulkan instance extension not available: %s", desc.requiredExts[i]);
            goto err_return;
        }
        extensions[ext_count++] = desc.requiredExts[i];
    }

    /* Best-effort validation support. */
    const char *layers[4];
    uint32_t layer_count = 0;
    if (desc.enableValidationLayer) {
        const char *val_layer = "VK_LAYER_KHRONOS_validation";
        bool has_val_layer = false;
        for (uint32_t i = 0; i < avail_layer_count && availLayers; i++) {
            if (strcmp(availLayers[i].layerName, val_layer) == 0) {
                has_val_layer = true;
                break;
            }
        }
        if (has_val_layer) {
            layers[layer_count++] = val_layer;

            /* Debug utils is optional; we wire it up later. */
            for (uint32_t i = 0; i < availExtsCount && availExts; i++) {
                if (strcmp(availExts[i].extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
                    extensions[ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
                    break;
                }
            }
        } else {
            LOG_WARN("validation requested but VK_LAYER_KHRONOS_validation not available");
        }
    }

    VkApplicationInfo app_info;
    memset(&app_info, 0, sizeof(app_info));
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Obsidian";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.pEngineName = "Obsidian";
    app_info.engineVersion = VK_MAKE_VERSION(0, 0, 1);
    app_info.apiVersion = VK_API_VERSION_1_4;

    VkInstanceCreateInfo ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app_info;
    ici.enabledExtensionCount = ext_count;
    ici.ppEnabledExtensionNames = extensions;
    ici.enabledLayerCount = layer_count;
    ici.ppEnabledLayerNames = layer_count ? layers : NULL;

    VkResult vr = vkCreateInstance(&ici, NULL, &instance);
    if (vr != VK_SUCCESS || instance == VK_NULL_HANDLE) {
        LOG_ERROR("ne_instance_create: vkCreateInstance failed: %d", (int)vr);
        goto err_return;
    }

    if (!vk_load_instance_fns(instance)) {
        LOG_ERROR("vk_instance_create: failed to load vulkan functions ");
        goto err_return;
    }

    return instance;

err_return:
    vk_uninit();
    return NULL;
}

void vk_instance_destroy(VkInstance *instance) {
    vkDestroyInstance(*instance, NULL);
    *instance = VK_NULL_HANDLE;

    if (ctx.isInit) {
        vk_uninit();
        ctx.isInit = false;
    } 
}

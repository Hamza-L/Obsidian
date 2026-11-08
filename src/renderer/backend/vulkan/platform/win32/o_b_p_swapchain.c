/*
 * DXGI + DirectComposition swapchain backend.
 *
 * Creates a DXGI swapchain with DXGI_ALPHA_MODE_PREMULTIPLIED and presents
 * through DirectComposition, enabling per-pixel transparency. Back buffers
 * are shared with Vulkan via VK_KHR_external_memory_win32 so the renderer
 * draws into them with its existing pipeline.
 *
 * Sync model (GPU-only, two shared timeline fences):
 *   renderSemaphore/renderFence: Vulkan signals N+1 once presented frame N is drawn.
 *   copySemaphore/copyFence:     D3D11 signals N+1 once presented frame N is copied out.
 *   Each pair is one kernel object: a shared ID3D11Fence whose D3D12 fence handle is
 *   imported into a Vulkan timeline semaphore (drivers like NVIDIA import but don't export). Frame N uses shared[N % COUNT],
 *   so Vulkan waits copyFence >= N - COUNT + 1 before overwriting it. N is presentCount.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <initguid.h>

#include "o_log.h"

#include "renderer/backend/vulkan/platform/win32/o_b_p_swapchain.h"
#include "renderer/backend/vulkan/o_b_vulkan_limits.h"

#include <stdlib.h>
#include <string.h>

/*
 * dcomp.h is C++ only (uses references, overloads). We load
 * DCompositionCreateDevice dynamically and define the minimal COM interfaces in
 * C ourselves.
 */

/* DXGI GUIDs we reference (defined here to avoid linking dxguid.lib) */
DEFINE_GUID(IID_IDXGIDevice_NE,      0x54ec77fa, 0x1377, 0x44e6, 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c);
DEFINE_GUID(IID_IDXGIFactory2_NE,    0x50c83a1c, 0xe072, 0x4c48, 0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0);
DEFINE_GUID(IID_IDXGISwapChain2_NE,  0xa8be2ac4, 0x199f, 0x4946, 0xb3, 0x31, 0x79, 0x59, 0x9f, 0xb9, 0x8d, 0xe7);
DEFINE_GUID(IID_IDXGIResource1_NE,   0x30961379, 0x4609, 0x4a41, 0x99, 0x8e, 0x54, 0xfe, 0x56, 0x7e, 0xe0, 0xc1);
DEFINE_GUID(IID_ID3D11Texture2D_NE,  0x6f15aaf2, 0xd208, 0x4e89, 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c);
DEFINE_GUID(IID_ID3D11Device5_NE,        0x8ffde202, 0xa0e7, 0x45df, 0x9e, 0x01, 0xe8, 0x37, 0x80, 0x1b, 0x5e, 0xa0);
DEFINE_GUID(IID_ID3D11DeviceContext4_NE, 0x917600da, 0xf58c, 0x4c33, 0x98, 0xd8, 0x3e, 0x15, 0xb3, 0x90, 0xfa, 0x24);
DEFINE_GUID(IID_ID3D11Fence_NE,          0xaffde9d1, 0x1df7, 0x4bb7, 0x8a, 0x34, 0x0f, 0x46, 0x25, 0x1d, 0xab, 0x80);

/* DirectComposition GUIDs */
DEFINE_GUID(IID_IDCompositionDevice_NE, 0xC37EA93A, 0xE7AA, 0x450D, 0xB1, 0x6F, 0x97, 0x46, 0xCB, 0x04, 0x07, 0xF3);
DEFINE_GUID(IID_IDCompositionTarget_NE, 0xEACDD04C, 0x117E, 0x4E17, 0x88, 0xF4, 0xD1, 0xB1, 0x2B, 0x0E, 0x13, 0x00);
DEFINE_GUID(IID_IDCompositionVisual_NE, 0x4D93059D, 0x097B, 0x4651, 0x9A, 0x60, 0xF0, 0xF2, 0x51, 0x16, 0xE2, 0xF3);

typedef struct IDCompositionDevice_C IDCompositionDevice_C;
typedef struct IDCompositionTarget_C IDCompositionTarget_C;
typedef struct IDCompositionVisual_C IDCompositionVisual_C;

typedef struct IDCompositionDeviceVtbl {
    /* IUnknown */
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDCompositionDevice_C *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDCompositionDevice_C *This);
    ULONG (STDMETHODCALLTYPE *Release)(IDCompositionDevice_C *This);
    /* IDCompositionDevice */
    HRESULT (STDMETHODCALLTYPE *Commit)(IDCompositionDevice_C *This);
    HRESULT (STDMETHODCALLTYPE *WaitForCommitCompletion)(IDCompositionDevice_C *This);
    HRESULT (STDMETHODCALLTYPE *GetFrameStatistics)(IDCompositionDevice_C *This, void *stats);
    HRESULT (STDMETHODCALLTYPE *CreateTargetForHwnd)(IDCompositionDevice_C *This, HWND hwnd, BOOL topmost, IDCompositionTarget_C **target);
    HRESULT (STDMETHODCALLTYPE *CreateVisual)(IDCompositionDevice_C *This, IDCompositionVisual_C **visual);
    /* remaining methods not needed */
} IDCompositionDeviceVtbl;

struct IDCompositionDevice_C { const IDCompositionDeviceVtbl *lpVtbl; };

typedef struct IDCompositionTargetVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDCompositionTarget_C *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDCompositionTarget_C *This);
    ULONG (STDMETHODCALLTYPE *Release)(IDCompositionTarget_C *This);
    HRESULT (STDMETHODCALLTYPE *SetRoot)(IDCompositionTarget_C *This, IDCompositionVisual_C *visual);
} IDCompositionTargetVtbl;

struct IDCompositionTarget_C { const IDCompositionTargetVtbl *lpVtbl; };

typedef struct IDCompositionVisualVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDCompositionVisual_C *This, REFIID riid, void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDCompositionVisual_C *This);
    ULONG (STDMETHODCALLTYPE *Release)(IDCompositionVisual_C *This);
    HRESULT (STDMETHODCALLTYPE *SetOffsetX_float)(IDCompositionVisual_C *This, float x);
    HRESULT (STDMETHODCALLTYPE *SetOffsetX_anim)(IDCompositionVisual_C *This, void *anim);
    HRESULT (STDMETHODCALLTYPE *SetOffsetY_float)(IDCompositionVisual_C *This, float y);
    HRESULT (STDMETHODCALLTYPE *SetOffsetY_anim)(IDCompositionVisual_C *This, void *anim);
    HRESULT (STDMETHODCALLTYPE *SetTransform_matrix)(IDCompositionVisual_C *This, const void *matrix);
    HRESULT (STDMETHODCALLTYPE *SetTransform_obj)(IDCompositionVisual_C *This, void *transform);
    HRESULT (STDMETHODCALLTYPE *SetTransformParent)(IDCompositionVisual_C *This, void *parent);
    HRESULT (STDMETHODCALLTYPE *SetEffect)(IDCompositionVisual_C *This, void *effect);
    HRESULT (STDMETHODCALLTYPE *SetBitmapInterpolationMode)(IDCompositionVisual_C *This, int mode);
    HRESULT (STDMETHODCALLTYPE *SetBorderMode)(IDCompositionVisual_C *This, int mode);
    HRESULT (STDMETHODCALLTYPE *SetClip_rect)(IDCompositionVisual_C *This, const void *rect);
    HRESULT (STDMETHODCALLTYPE *SetClip_obj)(IDCompositionVisual_C *This, void *clip);
    HRESULT (STDMETHODCALLTYPE *SetContent)(IDCompositionVisual_C *This, IUnknown *content);
    /* remaining methods not needed */
} IDCompositionVisualVtbl;

struct IDCompositionVisual_C { const IDCompositionVisualVtbl *lpVtbl; };

typedef HRESULT (WINAPI *PFN_DCompositionCreateDevice)(void *dxgiDevice, REFIID iid, void **dcompositionDevice);

/* VK_KHR_external_memory_win32 function (loaded at creation) */
static PFN_vkGetMemoryWin32HandlePropertiesKHR s_vkGetMemoryWin32HandlePropertiesKHR;

struct Swapchain {
    VkDevice device;
    VkPhysicalDevice phys;
    VkInstance instance;
    bool vsync;

    /* D3D11 (minimal — used only for DXGI resource ownership) */
    ID3D11Device *d3dDevice;
    IDXGISwapChain1 *dxgiSwapchain;

    /* DirectComposition */
    IDCompositionDevice_C *dcompDevice;
    IDCompositionTarget_C *dcompTarget;
    IDCompositionVisual_C *dcompVisual;
    HMODULE dcompLib;

    /* image info */
    VkFormat format;
    VkExtent2D extent;

    /* Intermediate shared textures (Vulkan renders into these, D3D11 copies to swapchain) */
    ID3D11Texture2D *sharedTextures[SWAPCHAIN_IMAGE_COUNT];
    HANDLE sharedHandles[SWAPCHAIN_IMAGE_COUNT];
    VkImage images[SWAPCHAIN_IMAGE_COUNT];
    VkImageView imageViews[SWAPCHAIN_IMAGE_COUNT];
    VkDeviceMemory vkMemories[SWAPCHAIN_IMAGE_COUNT];
    ID3D11DeviceContext *d3dContext;

    /* Shared timeline fences (D3D11.4) */
    ID3D11Device5 *d3dDevice5;
    ID3D11DeviceContext4 *d3dContext4;
    VkSemaphore renderSemaphore;
    VkSemaphore copySemaphore;
    ID3D11Fence *renderFence;
    ID3D11Fence *copyFence;
    uint64_t presentCount;

    /* Frame pacing */
    HANDLE frameLatencyWaitable;
    bool frameWaited;

    /* Set when a failure leaves the timelines unrecoverable: never present or wait on it again */
    bool lost;

    VkPhysicalDeviceMemoryProperties memProps;
};

static bool get_window_client_size(void *hwnd, int32_t *width, int32_t *height) {
    RECT r = {0};
    if (!hwnd || !GetClientRect(hwnd, &r)) return false;
    *width = r.right - r.left;
    *height = r.bottom - r.top;
    if (*width <= 0 || *height <= 0) return false;
    return true;
}

static uint32_t find_memory_type(Swapchain *sc, uint32_t type_filter, VkMemoryPropertyFlags properties) {
    for (uint32_t i = 0; i < sc->memProps.memoryTypeCount; i++) {
        if ((type_filter & (1u << i)) &&
            (sc->memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

static void release_vk_images(Swapchain *sc) {
    for (uint32_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; i++) {
        if (sc->vkMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(sc->device, sc->vkMemories[i], NULL);
            sc->vkMemories[i] = VK_NULL_HANDLE;
        }
        if (sc->sharedHandles[i]) {
            CloseHandle(sc->sharedHandles[i]);
            sc->sharedHandles[i] = NULL;
        }
        if (sc->sharedTextures[i]) {
            ID3D11Texture2D_Release(sc->sharedTextures[i]);
            sc->sharedTextures[i] = NULL;
        }

        if (sc->imageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(sc->device, sc->imageViews[i], NULL);
            sc->imageViews[i] = VK_NULL_HANDLE;
        }

        if (sc->images[i] != VK_NULL_HANDLE) {
            vkDestroyImage(sc->device, sc->images[i], NULL);
            sc->images[i] = VK_NULL_HANDLE;
        }
    }
}

/*
 * Create intermediate shared textures and import them into Vulkan.
 *
 * The DXGI swapchain buffers themselves can't be shared (they're owned by the
 * compositor). Instead we create our own D3D11 textures with SHARED_NTHANDLE,
 * import them into Vulkan as render targets, and at present time copy from the
 * shared texture to the swapchain back buffer via D3D11.
 *
 * Flow per buffer:
 *   D3D11CreateTexture2D(SHARED_NTHANDLE) → CreateSharedHandle → HANDLE
 *   VkImportMemoryWin32HandleInfoKHR → vkAllocateMemory → vkBindImageMemory
 */
static bool create_dxgi_buffers_and_import_to_vulkan(Swapchain *sc) {
    HRESULT hr;

    /* Get D3D11 device context for copy operations at present time */
    if (!sc->d3dContext) {
        ID3D11Device_GetImmediateContext(sc->d3dDevice, &sc->d3dContext);
    }

    for (uint32_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; i++) {
        /* Create a shared D3D11 texture that Vulkan can import */
        D3D11_TEXTURE2D_DESC tex_desc;
        memset(&tex_desc, 0, sizeof(tex_desc));
        tex_desc.Width = sc->extent.width;
        tex_desc.Height = sc->extent.height;
        tex_desc.MipLevels = 1;
        tex_desc.ArraySize = 1;
        tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        tex_desc.SampleDesc.Count = 1;
        tex_desc.Usage = D3D11_USAGE_DEFAULT;
        tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        tex_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;

        hr = ID3D11Device_CreateTexture2D(sc->d3dDevice, &tex_desc, NULL, &sc->sharedTextures[i]);
        if (FAILED(hr) || !sc->sharedTextures[i]) {
            LOG_ERROR("D3D11CreateTexture2D (shared %u) failed (hr=0x%08lx)", i, hr);
            return false;
        }

        /* Get shared handle from the texture */
        IDXGIResource1 *dxgi_resource = NULL;
        hr = ID3D11Texture2D_QueryInterface(sc->sharedTextures[i], &IID_IDXGIResource1_NE, (void **)&dxgi_resource);
        if (FAILED(hr) || !dxgi_resource) {
            LOG_ERROR("QueryInterface(IDXGIResource1) for shared tex %u failed (hr=0x%08lx)", i, hr);
            return false;
        }

        HANDLE shared_handle = NULL;
        hr = IDXGIResource1_CreateSharedHandle(
            dxgi_resource,
            NULL,
            DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
            NULL,
            &shared_handle);
        IDXGIResource1_Release(dxgi_resource);

        if (FAILED(hr) || !shared_handle) {
            LOG_ERROR("CreateSharedHandle(%u) failed (hr=0x%08lx)", i, hr);
            return false;
        }

        sc->sharedHandles[i] = shared_handle;

        /* Query what Vulkan memory types are compatible with this handle */
        VkMemoryWin32HandlePropertiesKHR handle_props;
        memset(&handle_props, 0, sizeof(handle_props));
        handle_props.sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR;

        VkResult vr = s_vkGetMemoryWin32HandlePropertiesKHR(
            sc->device,
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
            shared_handle,
            &handle_props);

        if (vr != VK_SUCCESS) {
            LOG_ERROR("vkGetMemoryWin32HandlePropertiesKHR(%u) failed (vr=%d)", i, (int)vr);
            return false;
        }

        /* Create VkImage backed by external memory */
        VkExternalMemoryImageCreateInfo ext_img_ci;
        memset(&ext_img_ci, 0, sizeof(ext_img_ci));
        ext_img_ci.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        ext_img_ci.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;

        VkImageCreateInfo img_ci;
        memset(&img_ci, 0, sizeof(img_ci));
        img_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_ci.pNext = &ext_img_ci;
        img_ci.imageType = VK_IMAGE_TYPE_2D;
        img_ci.format = sc->format;
        img_ci.extent.width = sc->extent.width;
        img_ci.extent.height = sc->extent.height;
        img_ci.extent.depth = 1;
        img_ci.mipLevels = 1;
        img_ci.arrayLayers = 1;
        img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
        img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        img_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        img_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        vr = vkCreateImage(sc->device, &img_ci, NULL, &sc->images[i]);
        if (vr != VK_SUCCESS) {
            LOG_ERROR("vkCreateImage (DXGI import %u) failed (vr=%d)", i, (int)vr);
            return false;
        }

        /* Get memory requirements to know allocation size */
        VkMemoryRequirements mem_req;
        vkGetImageMemoryRequirements(sc->device, sc->images[i], &mem_req);

        /* Find a compatible memory type from what the handle supports */
        uint32_t mem_type = find_memory_type(
            sc,
            handle_props.memoryTypeBits & mem_req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (mem_type == UINT32_MAX) {
            /* Fallback: try any compatible type without requiring device-local */
            mem_type = find_memory_type(sc, handle_props.memoryTypeBits & mem_req.memoryTypeBits, 0);
        }
        if (mem_type == UINT32_MAX) {
            LOG_ERROR("no compatible memory type for DXGI import (buffer %u)", i);
            return false;
        }

        /* Import the shared handle as Vulkan memory */
        VkImportMemoryWin32HandleInfoKHR import_info;
        memset(&import_info, 0, sizeof(import_info));
        import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
        import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
        import_info.handle = shared_handle;

        VkMemoryDedicatedAllocateInfo dedicated_info;
        memset(&dedicated_info, 0, sizeof(dedicated_info));
        dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated_info.pNext = &import_info;
        dedicated_info.image = sc->images[i];

        VkMemoryAllocateInfo alloc_info;
        memset(&alloc_info, 0, sizeof(alloc_info));
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.pNext = &dedicated_info;
        alloc_info.allocationSize = mem_req.size;
        alloc_info.memoryTypeIndex = mem_type;

        vr = vkAllocateMemory(sc->device, &alloc_info, NULL, &sc->vkMemories[i]);
        if (vr != VK_SUCCESS) {
            LOG_ERROR("vkAllocateMemory (DXGI import %u) failed (vr=%d)", i, (int)vr);
            return false;
        }

        vr = vkBindImageMemory(sc->device, sc->images[i], sc->vkMemories[i], 0);
        if (vr != VK_SUCCESS) {
            LOG_ERROR("vkBindImageMemory (DXGI import %u) failed (vr=%d)", i, (int)vr);
            return false;
        }

        /* Create image view */
        VkImageViewCreateInfo ivci;
        memset(&ivci, 0, sizeof(ivci));
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = sc->images[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = sc->format;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel = 0;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount = 1;

        vr = vkCreateImageView(sc->device, &ivci, NULL, &sc->imageViews[i]);
        if (vr != VK_SUCCESS) {
            LOG_ERROR("vkCreateImageView (DXGI import %u) failed (vr=%d)", i, (int)vr);
            return false;
        }
    }

    return true;
}

static bool create_shared_timeline(Swapchain *sc, VkSemaphore *semaphore, ID3D11Fence **fence) {
    HRESULT hr = ID3D11Device5_CreateFence(sc->d3dDevice5, 0, D3D11_FENCE_FLAG_SHARED, &IID_ID3D11Fence_NE, (void **)fence);
    if (FAILED(hr) || !*fence) {
        LOG_ERROR("ID3D11Device5::CreateFence (shared) failed (hr=0x%08lx)", hr);
        return false;
    }

    HANDLE handle = NULL;
    hr = ID3D11Fence_CreateSharedHandle(*fence, NULL, GENERIC_ALL, NULL, &handle);
    if (FAILED(hr) || !handle) {
        LOG_ERROR("ID3D11Fence::CreateSharedHandle failed (hr=0x%08lx)", hr);
        return false;
    }

    VkSemaphoreTypeCreateInfo type_ci = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 0,
    };
    VkSemaphoreCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &type_ci,
    };
    VkResult vr = vkCreateSemaphore(sc->device, &ci, NULL, semaphore);
    if (vr != VK_SUCCESS) {
        LOG_ERROR("vkCreateSemaphore (shared timeline) failed (vr=%d)", (int)vr);
        CloseHandle(handle);
        return false;
    }

    VkImportSemaphoreWin32HandleInfoKHR import_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR,
        .semaphore = *semaphore,
        .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT,
        .handle = handle,
    };
    vr = vkImportSemaphoreWin32HandleKHR(sc->device, &import_info);
    // Importing an NT handle does not take ownership of it: close it either way.
    CloseHandle(handle);
    if (vr != VK_SUCCESS) {
        LOG_ERROR("vkImportSemaphoreWin32HandleKHR failed (vr=%d)", (int)vr);
        return false;
    }
    return true;
}

static bool create_shared_fences(Swapchain *sc) {
    HRESULT hr = ID3D11Device_QueryInterface(sc->d3dDevice, &IID_ID3D11Device5_NE, (void **)&sc->d3dDevice5);
    if (FAILED(hr) || !sc->d3dDevice5) {
        LOG_ERROR("ID3D11Device5 unavailable, D3D11.4 is required (hr=0x%08lx)", hr);
        return false;
    }

    hr = ID3D11DeviceContext_QueryInterface(sc->d3dContext, &IID_ID3D11DeviceContext4_NE, (void **)&sc->d3dContext4);
    if (FAILED(hr) || !sc->d3dContext4) {
        LOG_ERROR("ID3D11DeviceContext4 unavailable, D3D11.4 is required (hr=0x%08lx)", hr);
        return false;
    }

    return create_shared_timeline(sc, &sc->renderSemaphore, &sc->renderFence) &&
           create_shared_timeline(sc, &sc->copySemaphore, &sc->copyFence);
}

static void wait_for_copies(Swapchain *sc) {
    if (!sc->d3dContext || !sc->copyFence || sc->lost) return;

    ID3D11DeviceContext_Flush(sc->d3dContext);
    if (sc->presentCount == 0) return;

    HANDLE done = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!done) {
        LOG_ERROR("CreateEventW failed (%lu)", GetLastError());
        return;
    }

    HRESULT hr = ID3D11Fence_SetEventOnCompletion(sc->copyFence, sc->presentCount, done);
    if (FAILED(hr)) {
        LOG_ERROR("ID3D11Fence::SetEventOnCompletion failed (hr=0x%08lx)", hr);
    } else if (WaitForSingleObject(done, 5000) != WAIT_OBJECT_0) {
        LOG_WARN("D3D11 copies did not finish within 5000ms");
    }
    CloseHandle(done);
}

static IDXGIAdapter *find_adapter_matching_vulkan_device(VkPhysicalDevice phys) {
    if (!vkGetPhysicalDeviceProperties2) {
        return NULL;
    }

    VkPhysicalDeviceIDProperties id_props;
    memset(&id_props, 0, sizeof(id_props));
    id_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

    VkPhysicalDeviceProperties2 props2;
    memset(&props2, 0, sizeof(props2));
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &id_props;

    vkGetPhysicalDeviceProperties2(phys, &props2);
    if (!id_props.deviceLUIDValid) {
        return NULL;
    }

    IDXGIFactory2 *factory = NULL;
    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory2_NE, (void **)&factory);
    if (FAILED(hr) || !factory) {
        LOG_ERROR("CreateDXGIFactory1 failed (hr=0x%08lx)", hr);
        return NULL;
    }

    IDXGIAdapter *match = NULL;
    for (UINT i = 0; !match; i++) {
        IDXGIAdapter1 *adapter = NULL;
        if (IDXGIFactory2_EnumAdapters1(factory, i, &adapter) != S_OK || !adapter) {
            break;
        }

        DXGI_ADAPTER_DESC1 adapter_desc;
        if (SUCCEEDED(IDXGIAdapter1_GetDesc1(adapter, &adapter_desc)) &&
            memcmp(&adapter_desc.AdapterLuid, id_props.deviceLUID, VK_LUID_SIZE) == 0) {
            match = (IDXGIAdapter *)adapter;
        } else {
            IDXGIAdapter1_Release(adapter);
        }
    }
    IDXGIFactory2_Release(factory);
    return match;
}

static bool dxgi_init(void *native_window_handle, Swapchain *sc) {
    HRESULT hr;

    HWND hwnd = (HWND)native_window_handle;
    if (!hwnd) {
        LOG_ERROR("ne_swapchain_dxgi: failed to get HWND");
        return false;
    }

    int32_t fb_w = 0, fb_h = 0;
    if(!get_window_client_size(hwnd, &fb_w, &fb_h)) return false;

    sc->extent.width = (uint32_t)fb_w;
    sc->extent.height = (uint32_t)fb_h;
    sc->format = VK_FORMAT_B8G8R8A8_UNORM;

    /* ── Create D3D11 device (minimal, for DXGI ownership) ──────────── */
    if (!sc->d3dDevice) {
        D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        IDXGIAdapter *adapter = find_adapter_matching_vulkan_device(sc->phys);
        if (!adapter) {
            LOG_ERROR("no DXGI adapter matches the Vulkan device LUID: textures and fences cannot be shared");
            return false;
        }

        hr = D3D11CreateDevice(
            adapter,
            D3D_DRIVER_TYPE_UNKNOWN,
            NULL,
            flags,
            &feature_level, 1,
            D3D11_SDK_VERSION,
            &sc->d3dDevice,
            NULL, NULL);

        IDXGIAdapter_Release(adapter);

        if (FAILED(hr) || !sc->d3dDevice) {
            LOG_ERROR("D3D11CreateDevice failed (hr=0x%08lx)", hr);
            return false;
        }
    }

    /* ── Create DXGI swapchain ──────────────────────────────────────── */
    if (!sc->dxgiSwapchain) {
        IDXGIDevice *dxgi_device = NULL;
        hr = ID3D11Device_QueryInterface(sc->d3dDevice, &IID_IDXGIDevice_NE, (void **)&dxgi_device);
        if (FAILED(hr)) {
            LOG_ERROR("QueryInterface(IDXGIDevice) failed (hr=0x%08lx)", hr);
            return false;
        }

        IDXGIAdapter *adapter = NULL;
        hr = IDXGIDevice_GetAdapter(dxgi_device, &adapter);
        IDXGIDevice_Release(dxgi_device);
        if (FAILED(hr)) {
            LOG_ERROR("IDXGIDevice::GetAdapter failed (hr=0x%08lx)", hr);
            return false;
        }

        IDXGIFactory2 *factory = NULL;
        hr = IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2_NE, (void **)&factory);
        IDXGIAdapter_Release(adapter);
        if (FAILED(hr)) {
            LOG_ERROR("IDXGIAdapter::GetParent(IDXGIFactory2) failed (hr=0x%08lx)", hr);
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 scd;
        memset(&scd, 0, sizeof(scd));
        scd.Width = sc->extent.width;
        scd.Height = sc->extent.height;
        scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        scd.SampleDesc.Count = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.BufferCount = SWAPCHAIN_IMAGE_COUNT;
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        scd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

        hr = IDXGIFactory2_CreateSwapChainForComposition(
            factory,
            (IUnknown *)sc->d3dDevice,
            &scd,
            NULL,
            &sc->dxgiSwapchain);
        IDXGIFactory2_Release(factory);

        if (FAILED(hr) || !sc->dxgiSwapchain) {
            LOG_ERROR("CreateSwapChainForComposition failed (hr=0x%08lx)", hr);
            return false;
        }

        IDXGISwapChain2 *sc2 = NULL;
        hr = IDXGISwapChain1_QueryInterface(sc->dxgiSwapchain, &IID_IDXGISwapChain2_NE, (void **)&sc2);
        if (FAILED(hr) || !sc2) {
            LOG_ERROR("QueryInterface(IDXGISwapChain2) failed (hr=0x%08lx)", hr);
            return false;
        }
        hr = IDXGISwapChain2_SetMaximumFrameLatency(sc2, 1);
        sc->frameLatencyWaitable = IDXGISwapChain2_GetFrameLatencyWaitableObject(sc2);
        IDXGISwapChain2_Release(sc2);
        if (FAILED(hr) || !sc->frameLatencyWaitable) {
            LOG_ERROR("frame latency waitable setup failed (hr=0x%08lx)", hr);
            return false;
        }
    }

    /* ── Setup DirectComposition (loaded dynamically, C interfaces) ─── */
    if (!sc->dcompDevice) {
        if (!sc->dcompLib) {
            sc->dcompLib = LoadLibraryA("dcomp.dll");
            if (!sc->dcompLib) {
                LOG_ERROR("failed to load dcomp.dll");
                return false;
            }
        }

        PFN_DCompositionCreateDevice pfnCreate = (PFN_DCompositionCreateDevice)GetProcAddress(sc->dcompLib, "DCompositionCreateDevice");
        if (!pfnCreate) {
            LOG_ERROR("failed to get DCompositionCreateDevice");
            return false;
        }

        hr = pfnCreate(NULL, &IID_IDCompositionDevice_NE, (void **)&sc->dcompDevice);
        if (FAILED(hr) || !sc->dcompDevice) {
            LOG_ERROR("DCompositionCreateDevice failed (hr=0x%08lx)", hr);
            return false;
        }

        hr = sc->dcompDevice->lpVtbl->CreateTargetForHwnd(sc->dcompDevice, hwnd, TRUE, &sc->dcompTarget);
        if (FAILED(hr) || !sc->dcompTarget) {
            LOG_ERROR("CreateTargetForHwnd failed (hr=0x%08lx)", hr);
            return false;
        }

        hr = sc->dcompDevice->lpVtbl->CreateVisual(sc->dcompDevice, &sc->dcompVisual);
        if (FAILED(hr) || !sc->dcompVisual) {
            LOG_ERROR("CreateVisual failed (hr=0x%08lx)", hr);
            return false;
        }

        hr = sc->dcompVisual->lpVtbl->SetContent(sc->dcompVisual, (IUnknown *)sc->dxgiSwapchain);
        if (FAILED(hr)) {
            LOG_ERROR("IDCompositionVisual::SetContent failed (hr=0x%08lx)", hr);
            return false;
        }

        hr = sc->dcompTarget->lpVtbl->SetRoot(sc->dcompTarget, sc->dcompVisual);
        if (FAILED(hr)) {
            LOG_ERROR("IDCompositionTarget::SetRoot failed (hr=0x%08lx)", hr);
            return false;
        }

        hr = sc->dcompDevice->lpVtbl->Commit(sc->dcompDevice);
        if (FAILED(hr)) {
            LOG_ERROR("IDCompositionDevice::Commit failed (hr=0x%08lx)", hr);
            return false;
        }
    }

    /* ── Load VK_KHR_external_memory_win32 function ────────────────── */
    if (!s_vkGetMemoryWin32HandlePropertiesKHR) {
        PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr_local = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(sc->instance, "vkGetDeviceProcAddr");
        if (vkGetDeviceProcAddr_local) {
            s_vkGetMemoryWin32HandlePropertiesKHR = (PFN_vkGetMemoryWin32HandlePropertiesKHR)vkGetDeviceProcAddr_local(sc->device, "vkGetMemoryWin32HandlePropertiesKHR");
        }
        if (!s_vkGetMemoryWin32HandlePropertiesKHR) {
            LOG_ERROR("failed to load vkGetMemoryWin32HandlePropertiesKHR \n(VK_KHR_external_memory_win32 not available?)");
            return false;
        }
    }

    if (!create_dxgi_buffers_and_import_to_vulkan(sc)) {
        return false;
    }

    if (!create_shared_fences(sc)) {
        return false;
    }

    return true;
}

/* ── Interface implementation ──────────────────────────────────────────── */

void swapchain_destroy(Swapchain *sc) {
    if (!sc) return;

    vkDeviceWaitIdle(sc->device);
    wait_for_copies(sc);

    release_vk_images(sc);

    if (sc->renderFence) { ID3D11Fence_Release(sc->renderFence); sc->renderFence = NULL; }
    if (sc->copyFence) { ID3D11Fence_Release(sc->copyFence); sc->copyFence = NULL; }
    if (sc->renderSemaphore) { vkDestroySemaphore(sc->device, sc->renderSemaphore, NULL); sc->renderSemaphore = VK_NULL_HANDLE; }
    if (sc->copySemaphore) { vkDestroySemaphore(sc->device, sc->copySemaphore, NULL); sc->copySemaphore = VK_NULL_HANDLE; }
    if (sc->d3dContext4) { ID3D11DeviceContext4_Release(sc->d3dContext4); sc->d3dContext4 = NULL; }
    if (sc->d3dDevice5) { ID3D11Device5_Release(sc->d3dDevice5); sc->d3dDevice5 = NULL; }
    if (sc->frameLatencyWaitable) { CloseHandle(sc->frameLatencyWaitable); sc->frameLatencyWaitable = NULL; }

    if (sc->d3dContext) { ID3D11DeviceContext_Release(sc->d3dContext); sc->d3dContext = NULL; }
    if (sc->dcompVisual) { sc->dcompVisual->lpVtbl->Release(sc->dcompVisual); sc->dcompVisual = NULL; }
    if (sc->dcompTarget) { sc->dcompTarget->lpVtbl->Release(sc->dcompTarget); sc->dcompTarget = NULL; }
    if (sc->dcompDevice) { sc->dcompDevice->lpVtbl->Release(sc->dcompDevice); sc->dcompDevice = NULL; }
    if (sc->dcompLib) { FreeLibrary(sc->dcompLib); sc->dcompLib = NULL; }
    if (sc->dxgiSwapchain) { IDXGISwapChain1_Release(sc->dxgiSwapchain); sc->dxgiSwapchain = NULL; }
    if (sc->d3dDevice) { ID3D11Device_Release(sc->d3dDevice); sc->d3dDevice = NULL; }

    free(sc);
}

bool swapchain_resize(Swapchain *sc, uint32_t width, uint32_t height) {
    if (sc->lost || width == 0 || height == 0) return false;

    vkDeviceWaitIdle(sc->device);
    wait_for_copies(sc);

    release_vk_images(sc);

    sc->extent.width = (uint32_t)width;
    sc->extent.height = (uint32_t)height;

    HRESULT hr = IDXGISwapChain1_ResizeBuffers(
        sc->dxgiSwapchain,
        SWAPCHAIN_IMAGE_COUNT,
        width, height,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);

    if (FAILED(hr)) {
        LOG_ERROR("IDXGISwapChain::ResizeBuffers failed (hr=0x%08lx)", hr);
        sc->lost = true;
        return false;
    }

    if (!create_dxgi_buffers_and_import_to_vulkan(sc)) {
        sc->lost = true;
        return false;
    }

    return true;
}

void swapchain_wait_for_frame(Swapchain *sc) {
    if (!sc || sc->frameWaited) return;

    if (WaitForSingleObjectEx(sc->frameLatencyWaitable, 1000, FALSE) != WAIT_OBJECT_0) {
        LOG_WARN("swapchain was not ready for a new frame within 1000ms");
    }
    sc->frameWaited = true;
}

bool swapchain_is_lost(const Swapchain *sc) {
    return sc->lost;
}

SwapchainFrameSync swapchain_frame_sync(const Swapchain *sc) {
    uint64_t frame = sc->presentCount;
    return (SwapchainFrameSync){
        .waitSemaphore = sc->copySemaphore,
        .waitValue = frame >= SWAPCHAIN_IMAGE_COUNT ? frame - SWAPCHAIN_IMAGE_COUNT + 1 : 0,
        .signalSemaphore = sc->renderSemaphore,
        .signalValue = frame + 1,
        .imageIndex = (uint32_t)(frame % SWAPCHAIN_IMAGE_COUNT),
    };
}

// The caller must already have submitted the Vulkan work that signals renderSemaphore to
// swapchain_frame_sync(sc).signalValue; otherwise the D3D11 wait below never completes.
SwapchainPresentResult swapchain_present(Swapchain *sc) {
    swapchain_wait_for_frame(sc);

    uint64_t frame = sc->presentCount;
    uint32_t index = (uint32_t)(frame % SWAPCHAIN_IMAGE_COUNT);
    uint64_t done = frame + 1;
    bool copied = false;

    HRESULT hr = ID3D11DeviceContext4_Wait(sc->d3dContext4, sc->renderFence, done);
    if (FAILED(hr)) {
        LOG_ERROR("ID3D11DeviceContext4::Wait(renderFence, %llu) failed (hr=0x%08lx)", (unsigned long long)done, hr);
    } else {
        ID3D11Texture2D *back_buffer = NULL;
        hr = IDXGISwapChain1_GetBuffer(sc->dxgiSwapchain, 0, &IID_ID3D11Texture2D_NE, (void **)&back_buffer);
        if (FAILED(hr) || !back_buffer) {
            LOG_ERROR("IDXGISwapChain::GetBuffer(0) failed (hr=0x%08lx)", hr);
        } else {
            ID3D11DeviceContext_CopyResource(sc->d3dContext, (ID3D11Resource *)back_buffer, (ID3D11Resource *)sc->sharedTextures[index]);
            ID3D11Texture2D_Release(back_buffer);
            copied = true;
        }
    }

    // Always signal, even if the copy was skipped: Vulkan waits on this value before reusing shared[index].
    hr = ID3D11DeviceContext4_Signal(sc->d3dContext4, sc->copyFence, done);
    if (FAILED(hr)) {
        LOG_ERROR("ID3D11DeviceContext4::Signal(copyFence, %llu) failed (hr=0x%08lx)", (unsigned long long)done, hr);
        sc->lost = true;
    }
    sc->presentCount = done;

    if (!copied || sc->lost) {
        ID3D11DeviceContext_Flush(sc->d3dContext);
        return SWAPCHAIN_PRESENT_FAILED;
    }

    UINT sync_interval = sc->vsync ? 1 : 0;
    DXGI_PRESENT_PARAMETERS params;
    memset(&params, 0, sizeof(params));

    hr = IDXGISwapChain1_Present1(sc->dxgiSwapchain, sync_interval, 0, &params);

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        LOG_ERROR("DXGI device lost (hr=0x%08lx)", hr);
        sc->lost = true;
        return SWAPCHAIN_PRESENT_FAILED;
    }

    if (FAILED(hr)) {
        LOG_ERROR("IDXGISwapChain::Present1 failed (hr=0x%08lx)", hr);
        ID3D11DeviceContext_Flush(sc->d3dContext);
        return SWAPCHAIN_PRESENT_FAILED;
    }

    // Only a successful Present1 returns a token to the latency waitable.
    sc->frameWaited = false;
    return SWAPCHAIN_PRESENT_SUCCESS;
}

Swapchain *swapchain_create(void *native_window_handle, SwapchainDesc desc) {
    if (!desc.device || !desc.physDevice || !desc.instance) {
        return NULL;
    }

    Swapchain *sc = (Swapchain*)calloc(1, sizeof(Swapchain));
    if (!sc) return NULL;

    sc->device = desc.device;
    sc->phys = desc.physDevice;
    sc->instance = desc.instance;
    sc->vsync = desc.vsync;

    vkGetPhysicalDeviceMemoryProperties(sc->phys, &sc->memProps);

    if (!dxgi_init(native_window_handle, sc)) {
        swapchain_destroy(sc);
        return NULL;
    }

    return sc;
}

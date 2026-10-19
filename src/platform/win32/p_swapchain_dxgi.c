/*
 * DXGI + DirectComposition swapchain backend.
 *
 * Creates a DXGI swapchain with DXGI_ALPHA_MODE_PREMULTIPLIED and presents
 * through DirectComposition, enabling per-pixel transparency. Back buffers
 * are shared with Vulkan via VK_KHR_external_memory_win32 so the renderer
 * draws into them with its existing pipeline.
 *
 * Sync model (CPU fence wait):
 *   The renderer submits to Vulkan with a fence. Before calling Present1()
 *   we wait on that fence to guarantee the GPU has finished rendering. This
 *   adds ~0.1ms of CPU latency but is simple and correct.
 */
#include "ne_swapchain.h"

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <initguid.h>

#include "p_vulkan_loader.h"
#include "p_vulkan_limits.h"

#include "o_window.h"
#include "o_log.h"

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
DEFINE_GUID(IID_IDXGISwapChain3_NE,  0x94d99bdb, 0xf1f8, 0x4ab0, 0xb2, 0x36, 0x7d, 0xa0, 0x17, 0x0e, 0xda, 0xb1);
DEFINE_GUID(IID_IDXGIResource1_NE,   0x30961379, 0x4609, 0x4a41, 0x99, 0x8e, 0x54, 0xfe, 0x56, 0x7e, 0xe0, 0xc1);
DEFINE_GUID(IID_ID3D11Texture2D_NE,  0x6f15aaf2, 0xd208, 0x4e89, 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c);
DEFINE_GUID(IID_IDXGIKeyedMutex_NE,  0x9d8e1289, 0xd7b3, 0x465f, 0x81, 0x26, 0x25, 0x0e, 0x34, 0x9a, 0xf8, 0x5d);

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

    /* Sync: CPU fence for present safety */
    VkFence presentFence;
    bool presentFenceSubmitted;
    bool needs_recreate;

    uint32_t acquiredImageIndex;

    VkPhysicalDeviceMemoryProperties memProps;
};

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

        if (sc->images[i] != VK_NULL_HANDLE) {
            vkDestroyImage(sc->device, sc->images[i], NULL);
        }

        if (sc->imageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(sc->device, sc->imageViews[i], NULL);
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
        tex_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

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

static bool dxgi_init(OWindow *window, Swapchain *sc) {
    HRESULT hr;

    HWND hwnd = (HWND)window_native_handle(window);
    if (!hwnd) {
        LOG_ERROR("ne_swapchain_dxgi: failed to get HWND");
        return false;
    }

    int32_t fb_w = 0, fb_h = 0;
    if (!window_get_content_size(window, &fb_w, &fb_h)) return false;
    if (fb_w <= 0 || fb_h <= 0) return false;

    sc->extent.width = (uint32_t)fb_w;
    sc->extent.height = (uint32_t)fb_h;
    sc->format = VK_FORMAT_B8G8R8A8_UNORM;

    /* ── Create D3D11 device (minimal, for DXGI ownership) ──────────── */
    if (!sc->d3dDevice) {
        D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        hr = D3D11CreateDevice(
            NULL,
            D3D_DRIVER_TYPE_HARDWARE,
            NULL,
            flags,
            &feature_level, 1,
            D3D11_SDK_VERSION,
            &sc->d3dDevice,
            NULL, NULL);

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
        scd.Flags = 0;

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

    /* ── Create present fence ──────────────────────────────────────── */
    if (sc->presentFence == VK_NULL_HANDLE) {
        VkFenceCreateInfo fci;
        memset(&fci, 0, sizeof(fci));
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VkResult vr = vkCreateFence(sc->device, &fci, NULL, &sc->presentFence);
        if (vr != VK_SUCCESS) {
            LOG_ERROR("vkCreateFence (present) failed (vr=%d)", (int)vr);
            return false;
        }
    }

    sc->needs_recreate = false;
    return true;
}

/* ── Interface implementation ──────────────────────────────────────────── */

void swapchain_destroy(Swapchain *sc) {
    vkDeviceWaitIdle(sc->device);

    release_vk_images(sc);

    if (sc->presentFence != VK_NULL_HANDLE) {
        vkDestroyFence(sc->device, sc->presentFence, NULL);
        sc->presentFence = VK_NULL_HANDLE;
    }

    if (sc->d3dContext) { ID3D11DeviceContext_Release(sc->d3dContext); sc->d3dContext = NULL; }
    if (sc->dcompVisual) { sc->dcompVisual->lpVtbl->Release(sc->dcompVisual); sc->dcompVisual = NULL; }
    if (sc->dcompTarget) { sc->dcompTarget->lpVtbl->Release(sc->dcompTarget); sc->dcompTarget = NULL; }
    if (sc->dcompDevice) { sc->dcompDevice->lpVtbl->Release(sc->dcompDevice); sc->dcompDevice = NULL; }
    if (sc->dcompLib) { FreeLibrary(sc->dcompLib); sc->dcompLib = NULL; }
    if (sc->dxgiSwapchain) { IDXGISwapChain1_Release(sc->dxgiSwapchain); sc->dxgiSwapchain = NULL; }
    if (sc->d3dDevice) { ID3D11Device_Release(sc->d3dDevice); sc->d3dDevice = NULL; }

    free(sc);
}

bool swapchain_recreate(OWindow *window, Swapchain *sc) {
    vkDeviceWaitIdle(sc->device);

    release_vk_images(sc);

    int32_t fb_w = 0, fb_h = 0;
    if (!window_get_content_size(window, &fb_w, &fb_h)) return false;
    if (fb_w <= 0 || fb_h <= 0) return false;

    sc->extent.width = (uint32_t)fb_w;
    sc->extent.height = (uint32_t)fb_h;

    HRESULT hr = IDXGISwapChain1_ResizeBuffers(
        sc->dxgiSwapchain,
        SWAPCHAIN_IMAGE_COUNT,
        (UINT)fb_w, (UINT)fb_h,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        0);

    if (FAILED(hr)) {
        LOG_ERROR("IDXGISwapChain::ResizeBuffers failed (hr=0x%08lx)", hr);
        return false;
    }

    if (!create_dxgi_buffers_and_import_to_vulkan(sc)) {
        return false;
    }

    sc->needs_recreate = false;
    return true;
}

SwapchainAcquireResult swapchain_acquire(Swapchain *sc, VkSemaphore signal_sem) {
    (void)signal_sem;
    /*
     * DXGI flip model: GetCurrentBackBufferIndex() returns the index of the
     * buffer that is not currently being presented or queued for presentation.
     * It's safe to render into immediately — DXGI guarantees this.
     *
     * Note: unlike Vulkan WSI, there's no GPU semaphore signaled here.
     * The image is ready for rendering as soon as this call returns.
     * The signal_sem is unused (the renderer's fence-wait at frame start
     * already ensures the previous frame's commands are done).
     */
    IDXGISwapChain3 *sc3 = NULL;
    HRESULT hr = sc->dxgiSwapchain->lpVtbl->QueryInterface((IDXGISwapChain1 *)sc->dxgiSwapchain, &IID_IDXGISwapChain3_NE, (void **)&sc3);
    if (FAILED(hr) || !sc3) {
        LOG_ERROR("QueryInterface(IDXGISwapChain3) failed (hr=0x%08lx)", hr);
        return SWAPCHAIN_ACQUIRE_FAILED;
    }

    UINT index = sc3->lpVtbl->GetCurrentBackBufferIndex(sc3);
    sc3->lpVtbl->Release((IDXGISwapChain3 *)sc3);

    if (index >= SWAPCHAIN_IMAGE_COUNT) {
        return SWAPCHAIN_ACQUIRE_FAILED;
    }

    sc->acquiredImageIndex = index;
    return SWAPCHAIN_ACQUIRE_SUCCESS;
}

SwapchainPresentResult swapchain_present(Swapchain *sc, VkQueue queue, VkSemaphore wait_sem, VkFence render_fence) {
    (void)queue;
    (void)wait_sem;

    /* The CPU-mediated copy needs this frame to finish, not the entire device.
     * Standard Vulkan WSI avoids this CPU wait and is the default demo path. */
    if (render_fence == VK_NULL_HANDLE ||
        vkWaitForFences(sc->device, 1, &render_fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        return SWAPCHAIN_PRESENT_FAILED;
    }

    /* Acquire keyed mutex on the shared texture, copy to swapchain, then release */
    uint32_t image_index = sc->acquiredImageIndex;

    IDXGIKeyedMutex *mutex = NULL;
    HRESULT hr = ID3D11Texture2D_QueryInterface(sc->sharedTextures[image_index],
        &IID_IDXGIKeyedMutex_NE, (void **)&mutex);
    if (SUCCEEDED(hr) && mutex) {
        hr = IDXGIKeyedMutex_AcquireSync(mutex, 0, 5000);
        if (SUCCEEDED(hr)) {
            ID3D11Texture2D *back_buffer = NULL;
            hr = IDXGISwapChain1_GetBuffer(sc->dxgiSwapchain, image_index, &IID_ID3D11Texture2D_NE, (void **)&back_buffer);
            if (SUCCEEDED(hr) && back_buffer) {
                ID3D11DeviceContext_CopyResource(sc->d3dContext, (ID3D11Resource *)back_buffer, (ID3D11Resource *)sc->sharedTextures[image_index]);
                ID3D11Texture2D_Release(back_buffer);
            }
            IDXGIKeyedMutex_ReleaseSync(mutex, 0);
        }
        IDXGIKeyedMutex_Release(mutex);
    }

    UINT sync_interval = sc->vsync ? 1 : 0;
    DXGI_PRESENT_PARAMETERS params;
    memset(&params, 0, sizeof(params));

    hr = IDXGISwapChain1_Present1(sc->dxgiSwapchain, sync_interval, 0, &params);

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        LOG_ERROR("DXGI device lost (hr=0x%08lx)", hr);
        return SWAPCHAIN_PRESENT_FAILED;
    }

    if (FAILED(hr)) {
        LOG_ERROR("IDXGISwapChain::Present1 failed (hr=0x%08lx)", hr);
        return SWAPCHAIN_PRESENT_FAILED;
    }

    return SWAPCHAIN_PRESENT_SUCCESS;
}

Swapchain *swapchain_create(OWindow *window, SwapchainDesc desc) {
    if (!desc.device || !desc.physDevice || !desc.instance || !desc.window) {
        return NULL;
    }

    Swapchain *sc = (Swapchain*)calloc(1, sizeof(Swapchain));
    if (!sc) return NULL;

    sc->device = desc.device;
    sc->phys = desc.physDevice;
    sc->instance = desc.instance;
    sc->vsync = desc.vsync;

    vkGetPhysicalDeviceMemoryProperties(sc->phys, &sc->memProps);

    if (!dxgi_init(window, sc)) {
        swapchain_destroy(sc);
        return NULL;
    }

    return sc;
}

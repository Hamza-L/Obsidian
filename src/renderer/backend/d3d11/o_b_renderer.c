#include "o_log.h"

#include "renderer/backend/d3d11/o_b_renderer_internal.h"

#include "core/o_typedefs.h"

#include <d3dcompiler.h>
#include <string.h>

ORenderer g_renderer;

static const char k_triangle_hlsl[] =
    "struct VSOut { float4 pos : SV_Position; float3 color : COLOR; };\n"
    "VSOut vs_main(uint id : SV_VertexID) {\n"
    "    const float2 pos[3] = { float2(0.0, 0.5), float2(0.5, -0.5), float2(-0.5, -0.5) };\n"
    "    const float3 color[3] = { float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1) };\n"
    "    VSOut o;\n"
    "    o.pos = float4(pos[id], 0, 1);\n"
    "    o.color = color[id];\n"
    "    return o;\n"
    "}\n"
    "float4 ps_main(VSOut i) : SV_Target { return float4(i.color, 1); }\n";

static DXGI_FORMAT dxgi_format(OFormat format) {
    switch (format) {
    case O_FORMAT_R8_UNORM: return DXGI_FORMAT_R8_UNORM;
    case O_FORMAT_RG8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
    case O_FORMAT_RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case O_FORMAT_RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case O_FORMAT_BGRA8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case O_FORMAT_BGRA8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case O_FORMAT_R16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
    case O_FORMAT_RGBA16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case O_FORMAT_R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
    case O_FORMAT_RG32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
    case O_FORMAT_RGBA32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case O_FORMAT_R32_UINT: return DXGI_FORMAT_R32_UINT;
    case O_FORMAT_DEPTH32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
    case O_FORMAT_NONE: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    }
    return DXGI_FORMAT_UNKNOWN;
}

static ID3DBlob *compile_hlsl(const char *entry, const char *target) {
    ID3DBlob *code = NULL, *errors = NULL;
    HRESULT hr = D3DCompile(k_triangle_hlsl, sizeof(k_triangle_hlsl) - 1, "triangle.hlsl", NULL, NULL,
                            entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
    if (FAILED(hr)) {
        LOG_ERROR("D3DCompile(%s) failed (hr=0x%08lx): %s", entry, hr,
                  errors ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "no error message");
    }
    if (errors) ID3D10Blob_Release(errors);
    return code;
}

static bool create_triangle_pipeline(ORenderer *renderer) {
    ID3DBlob *vs = compile_hlsl("vs_main", "vs_5_0");
    ID3DBlob *ps = compile_hlsl("ps_main", "ps_5_0");
    bool ok = vs && ps;
    ok = ok && SUCCEEDED(ID3D11Device_CreateVertexShader(renderer->device, ID3D10Blob_GetBufferPointer(vs), ID3D10Blob_GetBufferSize(vs), NULL, &renderer->triangleVS));
    ok = ok && SUCCEEDED(ID3D11Device_CreatePixelShader(renderer->device, ID3D10Blob_GetBufferPointer(ps), ID3D10Blob_GetBufferSize(ps), NULL, &renderer->trianglePS));
    if (vs) ID3D10Blob_Release(vs);
    if (ps) ID3D10Blob_Release(ps);
    if (!ok) return false;

    D3D11_RASTERIZER_DESC rd = {.FillMode = D3D11_FILL_SOLID, .CullMode = D3D11_CULL_NONE, .DepthClipEnable = TRUE};
    return SUCCEEDED(ID3D11Device_CreateRasterizerState(renderer->device, &rd, &renderer->noCull));
}

ORenderer *renderer_create(ORendererDesc desc) {
    (void)desc;
    memset(&g_renderer, 0, sizeof(g_renderer));

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (RENDERER_VALIDATION) flags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, &level, 1, D3D11_SDK_VERSION,
                                   &g_renderer.device, NULL, &g_renderer.context);
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        LOG_WARN("D3D11 debug layer unavailable (install the Graphics Tools optional feature), continuing without it");
        flags &= ~(UINT)D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, &level, 1, D3D11_SDK_VERSION,
                               &g_renderer.device, NULL, &g_renderer.context);
    }
    if (FAILED(hr)) {
        LOG_ERROR("D3D11CreateDevice failed (hr=0x%08lx)", hr);
        return NULL;
    }

    IDXGIDevice *dxgiDevice = NULL;
    IDXGIAdapter *adapter = NULL;
    if (FAILED(ID3D11Device_QueryInterface(g_renderer.device, &IID_IDXGIDevice, (void **)&dxgiDevice)) ||
        FAILED(IDXGIDevice_GetAdapter(dxgiDevice, &adapter)) ||
        FAILED(IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2, (void **)&g_renderer.factory))) {
        LOG_ERROR("failed to get the DXGI factory from the D3D11 device");
        if (adapter) IDXGIAdapter_Release(adapter);
        if (dxgiDevice) IDXGIDevice_Release(dxgiDevice);
        renderer_destroy(&g_renderer);
        return NULL;
    }
    IDXGIAdapter_Release(adapter);
    IDXGIDevice_Release(dxgiDevice);

    if (!create_triangle_pipeline(&g_renderer)) {
        LOG_ERROR("failed to create the triangle pipeline");
        renderer_destroy(&g_renderer);
        return NULL;
    }

    g_renderer.imagePool = memory_pool_create(sizeof(OImageSlot), MAX_IMAGE_COUNT);
    return &g_renderer;
}

void renderer_destroy(ORenderer *renderer) {
    if (!renderer) return;

    for (u32 i = 0; i < MAX_LAYER_COUNT; i++) {
        if (renderer->layerList.data[i].swapchain) IDXGISwapChain1_Release(renderer->layerList.data[i].swapchain);
    }
    // Images are not tracked individually here: release the ones the app never destroyed.
    for (u32 id = 1; renderer->imagePool && id <= MAX_IMAGE_COUNT; id++) {
        OImageSlot *slot = memory_pool_item_at(renderer->imagePool, id);
        if (!slot) break;
        if (slot->rtv) ID3D11RenderTargetView_Release(slot->rtv);
        if (slot->texture) ID3D11Texture2D_Release(slot->texture);
    }
    if (renderer->imagePool) memory_pool_destroy(renderer->imagePool);

    if (renderer->noCull) ID3D11RasterizerState_Release(renderer->noCull);
    if (renderer->trianglePS) ID3D11PixelShader_Release(renderer->trianglePS);
    if (renderer->triangleVS) ID3D11VertexShader_Release(renderer->triangleVS);
    if (renderer->factory) IDXGIFactory2_Release(renderer->factory);
    if (renderer->context) ID3D11DeviceContext_Release(renderer->context);
    if (renderer->device) ID3D11Device_Release(renderer->device);

    memset(renderer, 0, sizeof(*renderer));
}

void renderer_wait_idle(ORenderer *renderer) {
    ID3D11DeviceContext_Flush(renderer->context);
}

bool renderer_begin_frame(ORenderer *renderer) {
    renderer->presentMask = 0;
    return true;
}

void renderer_end_frame_and_present(ORenderer *renderer) {
    for (u32 i = 0; i < MAX_LAYER_COUNT; i++) {
        IDXGISwapChain1 *swapchain = renderer->layerList.data[i].swapchain;
        if (!swapchain || !(renderer->presentMask & (1u << i))) continue;

        DXGI_PRESENT_PARAMETERS params = {0};
        HRESULT hr = IDXGISwapChain1_Present1(swapchain, renderer->layerList.data[i].vsync ? 1 : 0, 0, &params);
        if (FAILED(hr)) LOG_ERROR("IDXGISwapChain1::Present1 failed (hr=0x%08lx)", hr);
    }
    renderer->presentMask = 0;
}

void renderer_begin_render(ORenderer *renderer, ORenderTargets targets) {
    if (renderer->rendering) {
        LOG_WARN("renderer is already recording a renderpass");
        return;
    }

    ID3D11RenderTargetView *rtvs[O_MAX_COLOR_ATTACHMENTS] = {0};
    u32 width = 0, height = 0;
    for (u32 i = 0; i < targets.color_count; i++) {
        OImage image = targets.colors[i].image;
        OImageSlot *slot = memory_pool_item_get(renderer->imagePool, (MHandle){.id = image.id, .age = image.age});
        if (!slot || !slot->rtv) {
            LOG_WARN("invalid color attachment at index %u", i);
            return;
        }
        rtvs[i] = slot->rtv;
        width = slot->desc.width;
        height = slot->desc.height;
        if (targets.colors[i].load == O_LOAD_CLEAR) {
            ID3D11DeviceContext_ClearRenderTargetView(renderer->context, slot->rtv, targets.colors[i].clear_color);
        }
    }

    D3D11_VIEWPORT viewport = {
        .TopLeftX = (FLOAT)targets.viewport.x,
        .TopLeftY = (FLOAT)targets.viewport.y,
        .Width = (FLOAT)(targets.viewport.width ? targets.viewport.width : width),
        .Height = (FLOAT)(targets.viewport.height ? targets.viewport.height : height),
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f,
    };
    ID3D11DeviceContext_RSSetViewports(renderer->context, 1, &viewport);
    ID3D11DeviceContext_OMSetRenderTargets(renderer->context, targets.color_count, rtvs, NULL);
    renderer->rendering = true;
}

void renderer_end_render(ORenderer *renderer) {
    if (!renderer->rendering) {
        LOG_WARN("renderer has no renderpass to end");
        return;
    }

    // Sample: there is no draw API yet, so every render pass draws the built-in triangle.
    ID3D11DeviceContext_IASetInputLayout(renderer->context, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(renderer->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_RSSetState(renderer->context, renderer->noCull);
    ID3D11DeviceContext_VSSetShader(renderer->context, renderer->triangleVS, NULL, 0);
    ID3D11DeviceContext_PSSetShader(renderer->context, renderer->trianglePS, NULL, 0);
    ID3D11DeviceContext_Draw(renderer->context, 3, 0);

    ID3D11DeviceContext_OMSetRenderTargets(renderer->context, 0, NULL, NULL);
    renderer->rendering = false;
}

OImage renderer_image_create(ORenderer *renderer, OImageDesc desc) {
    if (!desc.width || !desc.height) {
        LOG_ERROR("renderer_image_create: width and height are required");
        return (OImage){0};
    }
    if (!desc.usage) desc.usage = O_IMAGE_READ;

    MHandle h = memory_pool_item_acquire(renderer->imagePool);
    OImageSlot *slot = memory_pool_item_get(renderer->imagePool, h);
    if (!slot) return (OImage){0};
    memset(slot, 0, sizeof(*slot));
    slot->desc = desc;

    UINT bind = 0;
    if (desc.usage & O_IMAGE_READ) bind |= D3D11_BIND_SHADER_RESOURCE;
    if (desc.usage & O_IMAGE_WRITE) bind |= D3D11_BIND_UNORDERED_ACCESS;
    if (desc.usage & O_IMAGE_RENDERTARGET) bind |= D3D11_BIND_RENDER_TARGET;

    D3D11_TEXTURE2D_DESC td = {
        .Width = desc.width,
        .Height = desc.height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = dxgi_format(desc.format),
        .SampleDesc = {.Count = 1},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = bind,
    };
    HRESULT hr = ID3D11Device_CreateTexture2D(renderer->device, &td, NULL, &slot->texture);
    if (SUCCEEDED(hr) && (desc.usage & O_IMAGE_RENDERTARGET)) {
        hr = ID3D11Device_CreateRenderTargetView(renderer->device, (ID3D11Resource *)slot->texture, NULL, &slot->rtv);
    }
    if (FAILED(hr)) {
        LOG_ERROR("renderer_image_create failed (hr=0x%08lx)", hr);
        if (slot->texture) ID3D11Texture2D_Release(slot->texture);
        memset(slot, 0, sizeof(*slot));
        memory_pool_item_remove(renderer->imagePool, h);
        return (OImage){0};
    }
    return (OImage){.id = h.id, .age = h.age};
}

void renderer_image_destroy(ORenderer *renderer, OImage image) {
    MHandle h = {.id = image.id, .age = image.age};
    OImageSlot *slot = memory_pool_item_get(renderer->imagePool, h);
    if (!slot) return;

    // D3D11 keeps resources alive until the GPU is done with them, so release immediately.
    if (slot->rtv) ID3D11RenderTargetView_Release(slot->rtv);
    if (slot->texture) ID3D11Texture2D_Release(slot->texture);
    memset(slot, 0, sizeof(*slot));
    memory_pool_item_remove(renderer->imagePool, h);
}

OSurface renderer_surface_create(ORenderer *renderer, void *native_window_handle, OSurfaceDesc desc) {
    u32 index = MAX_LAYER_COUNT;
    for (u32 i = 0; i < MAX_LAYER_COUNT; i++) {
        if (!renderer->layerList.data[i].swapchain) { index = i; break; }
    }
    if (index == MAX_LAYER_COUNT) {
        LOG_WARN("OUT OF CAPACITY: attempting to create a surface beyond the capacity[%d]", MAX_LAYER_COUNT);
        return (OSurface){0};
    }

    DXGI_SWAP_CHAIN_DESC1 scd = {
        .Width = 0,
        .Height = 0,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
        .SampleDesc = {.Count = 1},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 2,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
    };
    HRESULT hr = IDXGIFactory2_CreateSwapChainForHwnd(renderer->factory, (IUnknown *)renderer->device,
                                                      (HWND)native_window_handle, &scd, NULL, NULL,
                                                      &renderer->layerList.data[index].swapchain);
    if (FAILED(hr)) {
        LOG_ERROR("CreateSwapChainForHwnd failed (hr=0x%08lx)", hr);
        return (OSurface){0};
    }
    renderer->layerList.data[index].vsync = desc.present_mode == O_PRESENT_VSYNC;
    return (OSurface){.id = index + 1, .age = renderer->layerList.data[index].age};
}

void renderer_surface_destroy(ORenderer *renderer, OSurface surface) {
    if (!surface.id || surface.id > MAX_LAYER_COUNT || surface.age != renderer->layerList.data[surface.id - 1].age) {
        LOG_WARN("attempting to destroy an already destroyed surface");
        return;
    }
    u32 index = surface.id - 1;
    if (renderer->layerList.data[index].swapchain) IDXGISwapChain1_Release(renderer->layerList.data[index].swapchain);
    renderer->layerList.data[index].swapchain = NULL;
    renderer->layerList.data[index].age++;
    renderer->presentMask &= ~(1u << index);
}

void renderer_surface_blit(ORenderer *renderer, OSurface surface, OImage image) {
    if (!surface.id || surface.id > MAX_LAYER_COUNT || surface.age != renderer->layerList.data[surface.id - 1].age ||
        !renderer->layerList.data[surface.id - 1].swapchain) {
        LOG_WARN("attempting to present an already destroyed surface");
        return;
    }
    OImageSlot *slot = memory_pool_item_get(renderer->imagePool, (MHandle){.id = image.id, .age = image.age});
    if (!slot) {
        LOG_WARN("renderer_surface_blit: invalid image");
        return;
    }

    u32 index = surface.id - 1;
    ID3D11Texture2D *backBuffer = NULL;
    HRESULT hr = IDXGISwapChain1_GetBuffer(renderer->layerList.data[index].swapchain, 0, &IID_ID3D11Texture2D, (void **)&backBuffer);
    if (FAILED(hr)) {
        LOG_ERROR("IDXGISwapChain1::GetBuffer failed (hr=0x%08lx)", hr);
        return;
    }

    D3D11_TEXTURE2D_DESC bd;
    ID3D11Texture2D_GetDesc(backBuffer, &bd);
    if (bd.Width == slot->desc.width && bd.Height == slot->desc.height && bd.Format == dxgi_format(slot->desc.format)) {
        ID3D11DeviceContext_CopyResource(renderer->context, (ID3D11Resource *)backBuffer, (ID3D11Resource *)slot->texture);
        renderer->presentMask |= 1u << index;
    } else {
        LOG_WARN("renderer_surface_blit: image %ux%u does not match the swapchain %ux%u", slot->desc.width, slot->desc.height, bd.Width, bd.Height);
    }
    ID3D11Texture2D_Release(backBuffer);
}

OShader renderer_shader_create(ORenderer *renderer, OShaderDesc desc) {
    (void)renderer;
    if (!shader_desc_is_valid(desc)) return (OShader){0};
    LOG_WARN("renderer_shader_create: not implemented on D3D11 yet");
    return (OShader){0};
}

void renderer_shader_update(ORenderer *renderer, OShader shader, OShaderDesc desc) {
    (void)renderer;
    (void)shader;
    (void)desc;
}

void renderer_shader_destroy(ORenderer *renderer, OShader shader) {
    (void)renderer;
    (void)shader;
}

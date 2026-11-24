#ifndef O_B_RENDERER_INTERNAL_H
#define O_B_RENDERER_INTERNAL_H

#include "o_renderer.h"
#include "o_memory.h"

#include "renderer/o_renderer_internal.h"

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#define MAX_SURFACE_COUNT 8
#define MAX_IMAGE_COUNT 256

typedef struct OImageSlot {
    OImageDesc desc;
    ID3D11Texture2D *texture;
    ID3D11RenderTargetView *rtv;
} OImageSlot;

typedef struct OSurfaceSlot {
    IDXGISwapChain1 *swapchain;
    bool vsync;
    bool inFrame;
    bool shouldPresent;
    uint32_t age;
} OSurfaceSlot;

struct ORenderer {
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    IDXGIFactory2 *factory;

    ID3D11VertexShader *triangleVS;
    ID3D11PixelShader *trianglePS;
    ID3D11RasterizerState *noCull;

    MPool imagePool;

    struct {
        OSurfaceSlot data[MAX_SURFACE_COUNT];
    } surfaceList;

    bool rendering;
};

#endif //O_B_RENDERER_INTERNAL_H

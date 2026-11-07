#ifndef R_B_RENDERER_INTERNAL_H
#define R_B_RENDERER_INTERNAL_H
#include "o_renderer.h"
#include "o_memory.h"

#include "renderer/o_renderer_internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define MAX_LAYER_COUNT 8
#define MAX_SHADER_COUNT 256

typedef struct OShaderSlot OShaderSlot;
typedef struct OImageSlot OImageSlot;
typedef struct Swapchain Swapchain;

struct ORenderer {
    uint64_t frameIndex;
    struct {
        struct {
            void *hwnd;
            Swapchain *swapchain;
            uint32_t age;
        } data[MAX_LAYER_COUNT] ;
    } layerList;

    MPool shaderPool;
    MPool imagePool;
};

struct OShaderSlot{
    OShaderStage stage;
    void * shader;
    bool compiled;
};

struct OImageSlot{
    OImageDesc desc;
    void * image;
};

#endif //R_B_RENDERER_INTERNAL_H

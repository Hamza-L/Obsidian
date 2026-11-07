#ifndef R_B_RENDERER_INTERNAL_H
#define R_B_RENDERER_INTERNAL_H

#include "o_renderer.h"
#include "o_memory.h"

#include "renderer/r_renderer.h"
#include "renderer/backend/metal/r_b_metal_limits.h"
#include "renderer/backend/metal/r_b_gpu_types.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

typedef struct {
    void * cpu;
    GPU_PTR(BUFFER) gpu;
    size_t offset;
} FrameArenaAlloc;

typedef struct {
    OImageDesc desc;
    __unsafe_unretained id<MTLTexture> gpuData;
} OImageSlot;

typedef struct {
    OShaderStage stage;
    __unsafe_unretained id<MTLLibrary> lib;
    union {
        __unsafe_unretained id<MTLComputePipelineState> compute;
        __unsafe_unretained id<MTLRenderPipelineState> graphics;
    } pso;
} OShaderSlot;

struct ORenderer{
    id<MTLDevice> device;
    id<MTL4CommandQueue> queue;
    id<MTL4Compiler> compiler;
    id<MTLResidencySet> residencySet;

    id<MTL4CommandAllocator> cmdAllocator[MAX_FRAMES_IN_FLIGHT]; //memory that holds the commands
    id<MTL4CommandBuffer> cmdBuffer; //where we record the commands and commit them to the queue
    id<MTLSharedEvent> sharedEvent; //sync object

    uint64_t frameIndex;

    id<MTL4RenderCommandEncoder> renderEncoder;

    struct {
        struct {
            NSView *view;
            CAMetalLayer *layer;
            id<CAMetalDrawable> currentDrawable;
            uint32_t age;
        } data[MAX_LAYER_COUNT] ;
    } layerList;

    MPool bufferPool;
    MPool imagePool;
    MPool shaderPool;

    struct {
        struct { OImage image; uint64_t frameAtDeletion;} *data ;
        uint32_t count;
        uint32_t capacity;
    } retireList;

    id<MTL4ArgumentTable> argumentTable;

    id<MTLBuffer> imageTable;

    id<MTLBuffer> frameArena;
    size_t frameArenaHead;
    size_t frameArenaEnd;
};

#endif //R_B_RENDERER_INTERNAL_H

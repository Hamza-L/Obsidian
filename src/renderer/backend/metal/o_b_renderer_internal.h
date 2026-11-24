#ifndef O_B_RENDERER_INTERNAL_H
#define O_B_RENDERER_INTERNAL_H

#include "o_renderer.h"
#include "o_memory.h"

#include "renderer/o_renderer_internal.h"
#include "renderer/backend/metal/o_b_metal_limits.h"
#include "renderer/backend/metal/o_b_gpu_types.h"

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
    uint32_t usedByFramesBitset;
} OImageSlot;

typedef struct {
    OShaderStage stage;
    __unsafe_unretained id<MTLLibrary> lib;
    union {
        __unsafe_unretained id<MTLComputePipelineState> compute;
        __unsafe_unretained id<MTLRenderPipelineState> graphics;
    } pso;
} OShaderSlot;

typedef struct {
    NSView *view;
    CAMetalLayer *layer;
    id<CAMetalDrawable> currentDrawable;

    id<MTL4CommandAllocator> cmdAllocator[MAX_FRAMES_IN_FLIGHT]; //memory that holds the commands
    id<MTLSharedEvent> frameEvent; //sync object

    uint64_t frameSignal[MAX_FRAMES_IN_FLIGHT]; //frameEvent value reached once the frame recorded with cmdAllocator[i] is done
    uint64_t frameNumber;
    uint32_t frameInFlight;
    uint32_t age;
} OLayerSlot;

struct ORenderer{
    id<MTLDevice> device;
    id<MTL4CommandQueue> queue;
    id<MTL4Compiler> compiler;
    id<MTLResidencySet> residencySet;

    id<MTL4CommandBuffer> cmdBuffer; //where we record the commands and commit them to the queue
    id<MTL4RenderCommandEncoder> renderEncoder;

    struct {
        OLayerSlot data[MAX_LAYER_COUNT];
    } layerList;
    OLayerSlot *currentLayer;

    MPool bufferPool;
    MPool imagePool;
    MPool shaderPool;

    struct {
        OImage *data;
        uint32_t count;
        uint32_t capacity;
    } retireList;

    id<MTL4ArgumentTable> argumentTable;

    id<MTLBuffer> imageTable;

    id<MTLBuffer> frameArena;
    size_t frameArenaHead;
    size_t frameArenaEnd;
};

#endif //O_B_RENDERER_INTERNAL_H

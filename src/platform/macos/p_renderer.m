#include "o_renderer.h"
#include "o_log.h"
#include "o_memory.h"
#include "o_window.h"

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <objc/runtime.h>
#include <dispatch/dispatch.h>

#include <stdlib.h>

// max number of allocations in the residency set
#define MAX_RESIDENCY_BUFFER_ALLOCS 1024
#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_LAYER_COUNT 8
#define INIT_IMAGE_RETIRE_COUNT 64

#define DEFAULT_IMAGE_DESC (OImageDesc) {.format = O_FORMAT_BGRA8_SRGB, .width = 0, .height = 0, .usage = O_IMAGE_READ}

struct ORenderer{
    id<MTLDevice> device;
    id<MTL4CommandQueue> queue;
    id<MTLResidencySet> residencySet;

    id<MTL4CommandAllocator> cmdAllocator[MAX_FRAMES_IN_FLIGHT]; //memory that holds the commands
    id<MTL4CommandBuffer> cmdBuffer; //where we record the commands and commit them to the queue
    id<MTLSharedEvent> sharedEvent; //sync object

    uint64_t frameIndex;

    id<MTL4RenderCommandEncoder> renderEncoder;

    MPool imagePool;

    struct {
        struct { id<MTLTexture> gpuData; uint64_t frameAtDeletion; } *data ;
        uint32_t count;
        uint32_t capacity;
    } retireList;

    struct {
        struct {
            NSView *view;
            CAMetalLayer *layer;
            id<CAMetalDrawable> currentDrawable;
            uint32_t age;
        } data[MAX_LAYER_COUNT] ;
    } layerList;

    // MTLBindingType argumentTable;
    // id<MTLBuffer> imageTable;

} g_renderer;

typedef struct {
    OImageDesc desc;
    id<MTLTexture> gpuData;
} OImageSlot;


static MTLPixelFormat mtl_pixel_format(OFormat format) {
    switch (format) {
    case O_FORMAT_R8_UNORM: return MTLPixelFormatR8Unorm;
    case O_FORMAT_RG8_UNORM: return MTLPixelFormatRG8Unorm;
    case O_FORMAT_RGBA8_UNORM: return MTLPixelFormatRGBA8Unorm;
    case O_FORMAT_RGBA8_SRGB: return MTLPixelFormatRGBA8Unorm_sRGB;
    case O_FORMAT_BGRA8_UNORM: return MTLPixelFormatBGRA8Unorm;
    case O_FORMAT_BGRA8_SRGB: return MTLPixelFormatBGRA8Unorm_sRGB;
    case O_FORMAT_R16_FLOAT: return MTLPixelFormatR16Float;
    case O_FORMAT_RGBA16_FLOAT: return MTLPixelFormatRGBA16Float;
    case O_FORMAT_R32_FLOAT: return MTLPixelFormatR32Float;
    case O_FORMAT_RG32_FLOAT: return MTLPixelFormatRG32Float;
    case O_FORMAT_RGBA32_FLOAT: return MTLPixelFormatRGBA32Float;
    case O_FORMAT_R32_UINT: return MTLPixelFormatR32Uint;
    case O_FORMAT_DEPTH32_FLOAT: return MTLPixelFormatDepth32Float;
    case O_FORMAT_NONE: return MTLPixelFormatInvalid;
    }
    return MTLPixelFormatInvalid;
}

static MTLTextureUsage mtl_texture_usage(OImageUsage usage) {
    MTLTextureUsage mtlusage = 0;
    if(usage & O_IMAGE_READ) mtlusage |= MTLTextureUsageShaderRead;
    if(usage & O_IMAGE_WRITE) mtlusage |= MTLTextureUsageShaderWrite;
    if(usage & O_IMAGE_RENDERTARGET) mtlusage |= MTLTextureUsageRenderTarget;
    return mtlusage;
}

static MTLLoadAction mtl_load_action(OLoadOp load) {
    switch (load) {
        case O_LOAD_CLEAR: return MTLLoadActionClear;
        case O_LOAD_KEEP: return MTLLoadActionLoad;
        case O_LOAD_DONT_CARE: return MTLLoadActionDontCare;
    }
    return MTLLoadActionDontCare;
}

static MTLStoreAction mtl_store_action(OStoreOp store) {
    switch (store) {
        case O_STORE_KEEP: return MTLStoreActionStore;
        case O_STORE_DONT_CARE: return MTLStoreActionDontCare;
    }
    return MTLStoreActionDontCare;
}

static void drain_all_images(ORenderer* renderer) {
    for (uint32_t i = 0; i < renderer->retireList.count; i++) {
        [renderer->residencySet removeAllocation: renderer->retireList.data[i].gpuData];
        CFBridgingRelease((__bridge CFTypeRef _Nullable)renderer->retireList.data[i].gpuData);

        //swap.
        renderer->retireList.data[i] = renderer->retireList.data[renderer->retireList.count - 1];
        renderer->retireList.count--;
        i--;
    }
}

static void drain_retired_images(ORenderer* renderer) {
    for (uint32_t i = 0; i < renderer->retireList.count; i++) {
        if ( renderer->retireList.data[i].frameAtDeletion  + MAX_FRAMES_IN_FLIGHT <= renderer->frameIndex) {
            [renderer->residencySet removeAllocation: renderer->retireList.data[i].gpuData];
            CFBridgingRelease((__bridge CFTypeRef _Nullable)renderer->retireList.data[i].gpuData);

            //swap.
            renderer->retireList.data[i] = renderer->retireList.data[renderer->retireList.count - 1];
            renderer->retireList.count--;
            i--;
        }
    }
}

ORenderer *renderer_create(ORendererDesc *desc) {
    (void)desc;

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        LOG_ERROR("failed to create Metal device");
        return NULL;
    }

    id<MTL4CommandQueue> queue = [device newMTL4CommandQueue];
    if (!queue) {
        LOG_ERROR("failed to create Metal command queue");
        return NULL;
    }


    MTLResidencySetDescriptor *rsdesc = [[MTLResidencySetDescriptor alloc] init];
    rsdesc.label = @"Primary Residency set";
    rsdesc.initialCapacity = MAX_RESIDENCY_BUFFER_ALLOCS;

    NSError * err = nil;
    id<MTLResidencySet> residencySet = [device newResidencySetWithDescriptor:rsdesc
                                                                       error: &err];

    if (!residencySet) {
        LOG_ERROR("failed to create Metal residency set: %s", err.localizedDescription.UTF8String);
        return NULL;
    }

    [queue addResidencySet: residencySet];

    g_renderer.device = device;
    g_renderer.queue = queue;
    g_renderer.residencySet = residencySet;

    if (!(g_renderer.cmdBuffer = [g_renderer.device newCommandBuffer])) {
        LOG_ERROR("failed to create command buffer"); return NULL;
    }

    if (!(g_renderer.sharedEvent = [g_renderer.device newSharedEvent])) {
        LOG_ERROR("failed to create shared event object"); return NULL;
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (!(g_renderer.cmdAllocator[i] = [g_renderer.device newCommandAllocator])) {
            LOG_ERROR("failed to create command allocator for index: %d", i); return NULL;
        }
    }

    g_renderer.imagePool = memory_pool_create(sizeof(OImageSlot));
    g_renderer.retireList.capacity = INIT_IMAGE_RETIRE_COUNT;
    g_renderer.retireList.count = 0;
    g_renderer.retireList.data = malloc(g_renderer.retireList.capacity * sizeof(*g_renderer.retireList.data));

    MTLCaptureDescriptor *cd = [MTLCaptureDescriptor new];
    cd.captureObject = g_renderer.device;
    cd.destination = MTLCaptureDestinationGPUTraceDocument;
    cd.outputURL = [NSURL fileURLWithPath:@"/tmp/first_frames.gputrace"];

    err = nil;
    if (![MTLCaptureManager.sharedCaptureManager startCaptureWithDescriptor:cd error:&err]) {
        LOG_ERROR("Capture : %s", err.localizedDescription.UTF8String);
    }

    return &g_renderer;
}

void renderer_wait_idle(ORenderer *renderer) {
    if (![renderer->sharedEvent waitUntilSignaledValue:(renderer->frameIndex) timeoutMS:5000]){
        LOG_WARN("GPU has been busy for longer than 5s\n");
    }
}

bool renderer_begin_frame(ORenderer *renderer) {
    uint64_t slot = renderer->frameIndex % MAX_FRAMES_IN_FLIGHT;
    if (renderer->frameIndex >= MAX_FRAMES_IN_FLIGHT) {
        if (![renderer->sharedEvent waitUntilSignaledValue:(renderer->frameIndex - MAX_FRAMES_IN_FLIGHT + 1) timeoutMS:1000]){
            LOG_WARN("GPU did not finish frame %llu within 1000ms\n", renderer->frameIndex - MAX_FRAMES_IN_FLIGHT + 1);
            return false;//return without recording anything new to give it another frame.
        }
    }

    drain_retired_images(renderer);

    [renderer->cmdAllocator[slot] reset]; //cmd allocator is an arena. here reset moves the pointer back to the start of the slot
    [renderer->cmdBuffer beginCommandBufferWithAllocator:renderer->cmdAllocator[slot]];
    return true;
}

void renderer_end_frame_and_present(ORenderer *renderer) {
    [renderer->cmdBuffer endCommandBuffer];

    [renderer->residencySet commit];

    [renderer->queue commit: &renderer->cmdBuffer count: 1];
    [renderer->queue signalEvent: renderer->sharedEvent value: renderer->frameIndex + 1];

    for (uint32_t i = 0; i < MAX_LAYER_COUNT; i++) {
        if (renderer->layerList.data[i].currentDrawable) {
            [renderer->queue signalDrawable:renderer->layerList.data[i].currentDrawable];
            [renderer->layerList.data[i].currentDrawable present];
            renderer->layerList.data[i].currentDrawable = nil;
        }
    }

    if (renderer->frameIndex >= 5) {
        [MTLCaptureManager.sharedCaptureManager stopCapture];
    }

    renderer->frameIndex++;
}

OImage renderer_image_create(ORenderer *renderer, OImageDesc *imgDesc) {
    if(memory_pool_is_oom(renderer->imagePool)) return (OImage){0};

    MHandle h = memory_pool_item_acquire(renderer->imagePool);
    OImageSlot* imgSlot = (OImageSlot*)memory_pool_item_get(renderer->imagePool, h);
    if(!imgSlot) return (OImage){0};

    imgSlot->desc = imgDesc ? *imgDesc : DEFAULT_IMAGE_DESC;

    MTLTextureDescriptor *desc = [[MTLTextureDescriptor alloc] init];
    desc.width = imgSlot->desc.width;
    desc.height = imgSlot->desc.height;
    desc.pixelFormat = mtl_pixel_format(imgSlot->desc.format);
    desc.usage = mtl_texture_usage(imgSlot->desc.usage);
    desc.storageMode = MTLStorageModePrivate;

    imgSlot->gpuData = (__bridge id<MTLTexture>)CFBridgingRetain([renderer->device newTextureWithDescriptor:desc]);
    if (imgSlot->gpuData) {
        [renderer->residencySet addAllocation:imgSlot->gpuData];
    } else {
        imgSlot->desc = DEFAULT_IMAGE_DESC;
        memory_pool_item_remove(renderer->imagePool, h);
        return (OImage){0};
    }
    return (OImage){.index = h.index, .age = h.age};
}

void renderer_begin_render(ORenderer *renderer, const ORenderTargets *targets) {
    if (renderer->renderEncoder) {
        LOG_WARN("renderer is already recording a renderpass");
        return; //render is already in progress
    }

    MTL4RenderPassDescriptor* pass = [MTL4RenderPassDescriptor new];
    for (uint32_t i = 0; i < targets->color_count; i++) {
        MHandle h = (MHandle){.index = targets->colors[i].image.index, .age = targets->colors[i].image.age};
        OImageSlot * imgSlot = (OImageSlot*)memory_pool_item_get(renderer->imagePool, h);
        if (!imgSlot) {
            LOG_WARN("invalid attachment [%d] at index : %d", h.index, i);
            return;
        }

        OAttachment attachment = targets->colors[i];

        pass.colorAttachments[i].texture = imgSlot->gpuData;
        pass.colorAttachments[i].loadAction = mtl_load_action(attachment.load);
        pass.colorAttachments[i].storeAction = mtl_store_action(attachment.store);
        pass.colorAttachments[i].clearColor = MTLClearColorMake(attachment.clear_color[0], attachment.clear_color[1], attachment.clear_color[2], attachment.clear_color[3]);
        //same for depth if the depth image is non-null
    }

    id<MTL4RenderCommandEncoder> renderEncoder = [renderer->cmdBuffer renderCommandEncoderWithDescriptor:pass];
    if (!renderEncoder) {
        LOG_ERROR("failed to create a render encoder");
        return;
    }

    if (targets->viewport.width && targets->viewport.height) {
        MTLViewport viewport;
        viewport.width = targets->viewport.width;
        viewport.height = targets->viewport.height;
        viewport.originX = targets->viewport.x;
        viewport.originY = targets->viewport.y;
        viewport.znear = 0;
        viewport.zfar = 1;
        [renderEncoder setViewport:viewport];
    }
    renderer->renderEncoder = renderEncoder;
}

void renderer_end_render(ORenderer *renderer) {
    if (!renderer->renderEncoder) {
        LOG_WARN("renderer has no renderpass to end");
        return; //no render is in progress
    }

    [renderer->renderEncoder endEncoding];
    renderer->renderEncoder = nil;
}

OSurface renderer_surface_create(ORenderer *renderer, void *native_window_handle, const OSurfaceDesc *desc) {
    // get the first free layer slot
    int32_t index = 0;
    for (int i = 0; i < MAX_LAYER_COUNT; i++) {
        if (renderer->layerList.data[i].layer == NULL) {
            index = i + 1;
            break;
        }
    }

    // return if all slots are busy
    if (!index) {
        LOG_WARN("OUT OF CAPACITY: attempting to create a surface beyond the capacity[8]\n");
        return (OSurface){0};
    }

    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.device = renderer->device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;
    layer.opaque = NO;
    layer.displaySyncEnabled = desc->present_mode == O_PRESENT_VSYNC ? YES : NO;

    NSView *view = (__bridge NSView *)native_window_handle;
    layer.contentsScale = view.window.backingScaleFactor;
    layer.drawableSize = CGSizeMake(view.bounds.size.width * view.window.backingScaleFactor,
                                    view.bounds.size.height * view.window.backingScaleFactor);
    view.layer = layer;

    [renderer->queue addResidencySet:layer.residencySet];

    renderer->layerList.data[index - 1].view = view;
    renderer->layerList.data[index - 1].layer = layer;
    uint32_t currAge = renderer->layerList.data[index - 1].age;
    return (OSurface){index, currAge};
}

void renderer_surface_blit(ORenderer *renderer, OSurface surface, OImage image) {
    if (!surface.index || surface.age != renderer->layerList.data[surface.index - 1].age) {
        LOG_WARN("attempting to present an already destroyed surface\n");
        return;
    }

    NSView *view = renderer->layerList.data[surface.index - 1].view;
    CAMetalLayer *layer= renderer->layerList.data[surface.index - 1].layer;

    if (layer.contentsScale != view.window.backingScaleFactor) {
        layer.contentsScale = view.window.backingScaleFactor;
    }

    if (layer.drawableSize.width != view.bounds.size.width * view.window.backingScaleFactor &&
        layer.drawableSize.height != view.bounds.size.height * view.window.backingScaleFactor) {
        layer.drawableSize = CGSizeMake(view.bounds.size.width * view.window.backingScaleFactor,
                                        view.bounds.size.height * view.window.backingScaleFactor);
    }

    id<CAMetalDrawable> currentDrawable = [layer nextDrawable];
    if (currentDrawable) {
        [renderer->queue waitForDrawable:currentDrawable];

        OImageSlot* imgSlot = (OImageSlot*)memory_pool_item_get(renderer->imagePool, (MHandle){.index = image.index, .age = image.age});
        if (imgSlot) {
            id<MTL4ComputeCommandEncoder> computeEncoder = [renderer->cmdBuffer computeCommandEncoder];
            if (!computeEncoder) {
                LOG_ERROR("failed to create a compute encoder");
                return;
            }

            [computeEncoder barrierAfterQueueStages:MTLStageFragment beforeStages:MTLStageBlit visibilityOptions:MTL4VisibilityOptionNone];
            [computeEncoder copyFromTexture:imgSlot->gpuData toTexture:currentDrawable.texture];
            [computeEncoder endEncoding];

            renderer->layerList.data[surface.index - 1].currentDrawable = currentDrawable;
        }
    }
}

void renderer_image_destroy(ORenderer *renderer, OImage image) {
    if(HANDLE_IS_NULL(image)) return;
    MHandle h = (MHandle){.index = image.index, .age = image.age};
    OImageSlot* imgSlot = memory_pool_item_get(renderer->imagePool, h);

    if (renderer->retireList.count >= renderer->retireList.capacity) {
        void* newData = realloc(renderer->retireList.data, renderer->retireList.capacity * 2 * sizeof(*renderer->retireList.data));
        if (!newData) {
            LOG_ERROR("renderer_image_destroy(): failed to reallocate retire list.");
            return;
        }
        renderer->retireList.data = newData;
        renderer->retireList.capacity *= 2;
    }

    if (imgSlot) {
        renderer->retireList.data[renderer->retireList.count].gpuData = imgSlot->gpuData;
        renderer->retireList.data[renderer->retireList.count].frameAtDeletion = renderer->frameIndex;

        renderer->retireList.count++;
        memory_pool_item_remove(renderer->imagePool, h);
    }
}

void renderer_surface_destroy(ORenderer *renderer, OSurface surface) {
    if (!surface.index || surface.age != renderer->layerList.data[surface.index - 1].age) {
        LOG_WARN("attempting to destroy an already destroyed surface\n");
        return;
    }

    [renderer->queue removeResidencySet:renderer->layerList.data[surface.index - 1].layer.residencySet];

    renderer->layerList.data[surface.index - 1].layer = nil;
    renderer->layerList.data[surface.index - 1].view = nil;
    renderer->layerList.data[surface.index - 1].age++;
}

void renderer_destroy(ORenderer *renderer){
    if (!renderer) return;
    renderer_wait_idle(renderer);
    drain_all_images(renderer);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        renderer->cmdAllocator[i] = nil;
    }

    renderer->sharedEvent = nil;
    renderer->cmdBuffer = nil;
    renderer->residencySet = nil;
    renderer->queue = nil;
    renderer->device = nil;

    memory_pool_destroy(renderer->imagePool);

    free(renderer->retireList.data);
    renderer->retireList.capacity = 0;
    renderer->retireList.count = 0;
}

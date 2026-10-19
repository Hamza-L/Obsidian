#include "o_renderer.h"
#include "o_log.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <objc/runtime.h>
#include <dispatch/dispatch.h>

#include <stdlib.h>

struct ORenderer{
    id<MTLDevice> device;
    id<MTL4CommandQueue> queue;
} g_renderer;

ORenderer *renderer_create(ORendererDesc *desc) {
    (void)desc;
    if (RENDERER_VALIDATION) {
        setenv("MTL_DEBUG_LAYER", "1", 0);
        setenv("MTL_SHADER_VALIDATION", "1", 0);
        setenv("MTL_DEBUG_LAYER_ERROR_MODE", "assert", 0);
        // setenv("MTL_CAPTURE_ENABLED", "1", 0);
    }

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

    g_renderer.device = device;
    g_renderer.queue = queue;

    return &g_renderer;
}

void renderer_destroy(ORenderer *renderer){
    if (!renderer) return;
    renderer->device = nil;
    renderer->queue = nil;
}

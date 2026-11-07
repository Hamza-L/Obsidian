#include "o_renderer.h"

#include "r_b_renderer_internal.h"

ORenderer g_renderer;

ORenderer *renderer_create(ORendererDesc desc) {
(void)desc;
return &g_renderer;
}

void renderer_destroy(ORenderer *renderer) {
    (void)renderer;
}

bool renderer_begin_frame(ORenderer *renderer) {
    (void)renderer;
    return false;
}

void renderer_end_frame_and_present(ORenderer *renderer) {
    (void)renderer;

}

void renderer_begin_render(ORenderer *renderer, ORenderTargets targets) {
    (void)renderer;
    (void)targets;

}

void renderer_end_render(ORenderer *renderer) {
    (void)renderer;

}

OImage renderer_image_create(ORenderer *renderer, OImageDesc desc) {
    (void)renderer;
    (void)desc;
    return(OImage){0};
}

void renderer_image_destroy(ORenderer *renderer, OImage image) {
    (void)renderer;
    (void)image;
}

OSurface renderer_surface_create(ORenderer *renderer, void *native_window_handle, OSurfaceDesc desc) {
    (void)renderer;
    (void)native_window_handle;
    (void)desc;
    return (OSurface){0};
}

void renderer_surface_destroy(ORenderer *renderer, OSurface surface) {
    (void)renderer;
    (void)surface;
}

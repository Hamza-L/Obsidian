#include "unity.c"

#include "o_log.h"
#include "o_window.h"
#include "o_renderer.h"
#include "o_input.h"

#include <stdio.h>

struct RenderData{
    ORenderer * renderer;
    OSurface surface;
    OImage renderTarget;
} renderData;

static void on_key(OInputSubscriber *input, OKeyEvent event, void *user) {
    (void)input;
    if (event.key == O_KEY_ESCAPE) {
        OWindow* window = (OWindow*)user;
        window_close(window);
    }
}

static void on_refresh_render(OWindow *window, void *user) {
    ORenderer* renderer = ((struct RenderData*)user)->renderer;
    OSurface surface = ((struct RenderData*)user)->surface;
    OImage rt = ((struct RenderData*)user)->renderTarget;

    int32_t width, height;
    window_get_pixel_size(window, &width, &height);
    if (renderer_begin_frame(renderer)) {
        {
            renderer_begin_render(
                renderer,
                &(ORenderTargets){
                .color_count = 1,
                .colors[0] = (OAttachment){.image = rt, .clear_color = {1.0f, 0.0f, 0.0f, 1.0f}, .load = O_LOAD_CLEAR},
                .viewport = (ORect){0,0,width,height}
            });

            renderer_end_render(renderer);
        }
        renderer_surface_blit(renderer, surface, rt);
        renderer_end_frame_and_present(renderer);
    }
}

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    log_set_level(O_LOG_DEBUG);

    OWindow * window = window_create(&(OWindowDesc){.width = WINDOW_WIDTH, .height = WINDOW_HEIGHT, .x = 0, .y = 0, .title = "Obsidian", .transparent = true, .undecorated = true});
    input_subscribe((OInputCallbacks){.on_key_down = on_key }, window);

    ORenderer *renderer = renderer_create(&(ORendererDesc){0});

    OSurface surface = renderer_surface_create(renderer, window_native_handle(window), &(OSurfaceDesc){.width = WINDOW_WIDTH, .height = WINDOW_HEIGHT, .present_mode = O_PRESENT_VSYNC});

    int32_t width, height;
    window_get_pixel_size(window, &width, &height);
    OImage rt = renderer_image_create(renderer, &(OImageDesc){.format = O_FORMAT_BGRA8_UNORM, .width = width, .height = height, .usage = O_IMAGE_READ | O_IMAGE_RENDERTARGET});
    if (HANDLE_IS_NULL(rt)) {
        LOG_ERROR("failed to create image\n");
        return 0;
    }

    renderData.renderTarget = rt;
    renderData.renderer = renderer;
    renderData.surface = surface;
    window_set_callbacks(window, &(OWindowCallbacks){.on_refresh = on_refresh_render}, &renderData);

    while (window_poll_events()) {
        input_poll_events();
    }

    renderer_destroy(renderer);
    window_destroy(window);

    return 0;
}

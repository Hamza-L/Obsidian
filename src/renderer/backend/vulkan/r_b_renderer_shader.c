#include "o_log.h"

#include "renderer/backend/vulkan/r_b_renderer_internal.h"

#include "glslang_c_shader_types.h"
#include "glslang_c_interface.h"
#include "glslang/Public/resource_limits_c.h"

static bool glslang_ready = false;

OShader renderer_shader_create(ORenderer *renderer, OShaderDesc desc) {
    (void)renderer;
    OShader shader = {0};
    if (!shader_desc_is_valid(desc)) return shader;

    if (!glslang_ready) {
        if (!glslang_initialize_process()) {
            LOG_ERROR("failed to initialize glslang");
            return shader;
        }
        glslang_ready = true;
    }

    return shader;
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

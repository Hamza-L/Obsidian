#include "renderer/o_renderer_internal.h"

#include "o_log.h"

bool shader_desc_is_valid(OShaderDesc desc) {
    if (!desc.data.binary != !desc.binarySize) {
        LOG_ERROR("shader binary pointer and size must be provided together\n");
        return false;
    }
    if (!desc.data.source != !desc.sourceSize) {
        LOG_ERROR("shader source pointer and size must be provided together\n");
        return false;
    }
    if (!desc.data.binary && !desc.data.source) {
        LOG_ERROR("no shader data provided. Cannot create shader\n");
        return false;
    }
    if (!desc.entry || !desc.entry[0]) {
        LOG_ERROR("no shader entry point provided. Cannot create shader\n");
        return false;
    }
    if (desc.data.binary && desc.data.source) {
        LOG_INFO("both binary and source shader data provided. using binary\n");
    }
    return true;
}

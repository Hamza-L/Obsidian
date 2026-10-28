#ifndef O_RENDERER_H
#define O_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

// Zero-initialized descs select defaults. A handle with index 0 is null.
// Destroying a resource the GPU is still using is safe; it is released once the GPU is done.
// Commands execute in submission order; the renderer inserts synchronization and layout transitions.
//
// Shader contract:
//   Shaders are native binaries: SPIR-V on Vulkan, metallib on Metal, WGSL on WebGPU.
//   Resource table, set 0 on Vulkan and WebGPU, buffer(0) on Metal:
//     binding 0  images[]    indexed by OImage.index, declared with the texture type matching the image
//     binding 1  samplers[]  indexed by OSampler
//     binding 2  buffers[]   indexed by OBuffer.index, byte offsets are passed alongside in constants
//   Constants: push constants on Vulkan, buffer(1) on Metal, at most O_MAX_CONSTANTS_SIZE bytes.
//   Vertex shaders pull their vertices from buffers[] with vertex_id; there is no vertex input layout.
//   Clip space is +Y up with depth 0..1, image origin is top-left, front faces are counter-clockwise.

typedef struct ORenderer ORenderer;

typedef struct OBuffer { uint32_t index, age; } OBuffer;
typedef struct OImage { uint32_t index, age; } OImage;
typedef struct OShader { uint32_t index, age; } OShader;
typedef struct OSurface { uint32_t index, age; } OSurface;

#define O_MAX_COLOR_ATTACHMENTS 8
#define O_MAX_CONSTANTS_SIZE 128

typedef enum OFormat {
    O_FORMAT_NONE = 0,
    O_FORMAT_R8_UNORM,
    O_FORMAT_RG8_UNORM,
    O_FORMAT_RGBA8_UNORM,
    O_FORMAT_RGBA8_SRGB,
    O_FORMAT_BGRA8_UNORM,
    O_FORMAT_BGRA8_SRGB,
    O_FORMAT_R16_FLOAT,
    O_FORMAT_RGBA16_FLOAT,
    O_FORMAT_R32_FLOAT,
    O_FORMAT_RG32_FLOAT,
    O_FORMAT_RGBA32_FLOAT,
    O_FORMAT_R32_UINT,
    O_FORMAT_DEPTH32_FLOAT,
} OFormat;

typedef enum OMemory {
    O_MEMORY_SHARED = 0,
    O_MEMORY_GPU_ONLY,
} OMemory;

typedef uint32_t OImageUsage;
enum {
    O_IMAGE_READ = 1u << 0,
    O_IMAGE_WRITE = 1u << 1,
    O_IMAGE_RENDERTARGET = 1u << 2,
};

typedef enum OShaderStage {
    O_SHADER_VERTEX = 0,
    O_SHADER_FRAGMENT,
    O_SHADER_COMPUTE,
} OShaderStage;

typedef enum OSampler {
    O_SAMPLER_NEAREST_CLAMP = 0,
    O_SAMPLER_NEAREST_REPEAT,
    O_SAMPLER_LINEAR_CLAMP,
    O_SAMPLER_LINEAR_REPEAT,
    O_SAMPLER_COUNT,
} OSampler;

typedef enum OPresentMode {
    O_PRESENT_VSYNC = 0,
    O_PRESENT_IMMEDIATE,
} OPresentMode;

typedef enum OLoadOp {
    O_LOAD_DONT_CARE = 0,
    O_LOAD_CLEAR,
    O_LOAD_KEEP,
} OLoadOp;

typedef enum OStoreOp {
    O_STORE_KEEP = 0,
    O_STORE_DONT_CARE,
} OStoreOp;

typedef enum OTopology {
    O_TOPOLOGY_TRIANGLES = 0,
    O_TOPOLOGY_TRIANGLE_STRIP,
    O_TOPOLOGY_LINES,
    O_TOPOLOGY_LINE_STRIP,
    O_TOPOLOGY_POINTS,
} OTopology;

typedef enum OCull {
    O_CULL_NONE = 0,
    O_CULL_BACK,
    O_CULL_FRONT,
} OCull;

typedef enum OBlend {
    O_BLEND_OPAQUE = 0,
    O_BLEND_ALPHA,
    O_BLEND_PREMULTIPLIED,
    O_BLEND_ADDITIVE,
} OBlend;

typedef enum OCompare {
    O_COMPARE_NONE = 0,
    O_COMPARE_LESS,
    O_COMPARE_LESS_EQUAL,
    O_COMPARE_GREATER,
    O_COMPARE_GREATER_EQUAL,
    O_COMPARE_EQUAL,
    O_COMPARE_ALWAYS,
} OCompare;

typedef enum OIndexFormat {
    O_INDEX_U32 = 0,
    O_INDEX_U16,
} OIndexFormat;

typedef struct ORendererDesc {
    uint32_t frames_in_flight;
    uint64_t frame_arena_size;
    uint32_t max_buffers;
    uint32_t max_images;
    uint32_t max_shaders;
} ORendererDesc;

typedef struct OSurfaceDesc {
    uint32_t width, height;
    OPresentMode present_mode;
} OSurfaceDesc;

typedef struct OBufferDesc {
    uint64_t size;
    OMemory memory;
} OBufferDesc;

typedef struct OImageDesc {
    uint32_t width, height;
    OFormat format;
    OImageUsage usage;
} OImageDesc;

typedef struct ORect {
    int32_t x, y;
    uint32_t width, height;
} ORect;

typedef struct OAttachment {
    OImage image;
    OLoadOp load;
    OStoreOp store;
    float clear_color[4];
    float clear_depth;
} OAttachment;

typedef struct ORenderTargets {
    OAttachment colors[O_MAX_COLOR_ATTACHMENTS];
    uint32_t color_count;
    OAttachment depth;
    ORect viewport;
} ORenderTargets;

typedef struct ODraw {
    OShader vertex;
    OShader fragment;
    OTopology topology;
    OCull cull;
    OBlend blend;
    OCompare depth_compare;
    bool depth_write;
    bool wireframe;
    ORect scissor;

    OBuffer index_buffer;
    OIndexFormat index_format;
    uint64_t index_buffer_offset;
    uint32_t index_count;
    uint32_t vertex_count;
    uint32_t first_vertex;
    uint32_t instance_count;
    uint32_t first_instance;

    const void *constants;
    uint32_t constants_size;
} ODraw;

ORenderer *renderer_create(ORendererDesc *desc);
void renderer_destroy(ORenderer *renderer);
void renderer_wait_idle(ORenderer *renderer);

bool renderer_begin_frame(ORenderer *renderer);
void renderer_end_frame_and_present(ORenderer *renderer);

void renderer_begin_render(ORenderer *renderer, const ORenderTargets *targets);
void renderer_end_render(ORenderer *renderer);

OImage renderer_image_create(ORenderer *renderer, OImageDesc *desc);
void renderer_image_destroy(ORenderer *renderer, OImage image);

OSurface renderer_surface_create(ORenderer *renderer, void *native_window_handle, const OSurfaceDesc *desc);
void renderer_surface_destroy(ORenderer *renderer, OSurface surface);

void renderer_surface_blit(ORenderer *renderer, OSurface surface, OImage image);


#endif

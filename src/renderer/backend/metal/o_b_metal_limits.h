#ifndef O_B_METAL_LIMITS_H
#define O_B_METAL_LIMITS_H

#define SWAPCHAIN_IMAGE_COUNT 3
#define MAX_FRAMES_IN_FLIGHT 2

#define MAX_LAYER_COUNT 8

#define INIT_IMAGE_RETIRE_COUNT 64
#define MAX_TEXTURE_BIND_COUNT 256

#define MAX_SHADER_COUNT 64

// max number of allocations in the residency set
#define MAX_RESIDENCY_BUFFER_ALLOCS 1024

//number of tables to bind to the argument table
#define MAX_BUFFER_BIND_COUNT 8

#define FRAME_ARENA_ALIGN 16
#define FRAME_ARENA_SIZE 4096

#endif //O_B_METAL_LIMITS_H

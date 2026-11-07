#ifndef R_B_GPU_TYPES_H
#define R_B_GPU_TYPES_H

#ifdef __METAL_VERSION__
  #define GPU_PTR(T)   device const T*
  #define GPU_TEX      texture2d<float>
#else
  #define GPU_PTR(T)   uint64_t
  #define GPU_TEX      MTLResourceID
#endif

#endif //R_B_GPU_TYPES_H

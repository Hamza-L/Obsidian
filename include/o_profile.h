#ifndef O_PROFILE_H
#define O_PROFILE_H

#include <time.h>
#include <stdio.h>

#define PROFILE_START(name) \
    struct timespec start_##name, end_##name; \
    timespec_get(&start_##name, TIME_UTC);

#define PROFILE_END(name) \
    timespec_get(&end_##name, TIME_UTC); \
    double elapsed_##name = (end_##name.tv_sec - start_##name.tv_sec) + \
                            (end_##name.tv_nsec - start_##name.tv_nsec) / 1000000000.0; \
    fprintf(stderr,"[PROFILE] %s took %.6f seconds\n", #name, elapsed_##name);

#endif //O_PROFILE_H

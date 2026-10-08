#include "o_log.h"
#include "o_memory.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#define PROFILE_START(name) \
    struct timespec start_##name, end_##name; \
    timespec_get(&start_##name, TIME_UTC);

#define PROFILE_END(name) \
    timespec_get(&end_##name, TIME_UTC); \
    double elapsed_##name = (end_##name.tv_sec - start_##name.tv_sec) + \
                            (end_##name.tv_nsec - start_##name.tv_nsec) / 1000000000.0; \
    printf("[PROFILE] %s took %.6f seconds\n", #name, elapsed_##name);

typedef struct {
    char type_name[64];
} EntryType;

typedef struct {
    char name[32];
} ProgLang;

typedef struct {
    EntryType type;
    uint32_t age;
    ProgLang fav_lang2;
} Entry;

#define MAX_ENTRY 1000000000
const EntryType e_types[] = {{"Robot"}, {"Human"}, {"Alien"}, {"Animal"}};
const char* langs[] = { "C", "C++", "Java", "Python", "Javascript", "Rust", "C#"};
const ProgLang p_langs[] = { {"C"}, {"C++"}, {"Java"}, {"Python"}, {"Javascript"}, {"Rust"}, {"C#"}};

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    PROFILE_START(create_random_entries);

    MPool pool = memory_pool_create(sizeof(Entry));

    for (int i = 0; i < MAX_ENTRY && !memory_pool_is_oom(pool); i++) {
        MHandle h = memory_pool_item_acquire(pool);
        Entry* e = memory_pool_item_get(pool, h);
        if(e){
            e->age = rand() % 99;
            e->type = e_types[rand() % (sizeof(e_types)/sizeof(e_types[0]))];
            e->fav_lang2 = p_langs[rand() % (sizeof(p_langs)/sizeof(p_langs[0]))];
        }
    }

    memory_pool_destroy(pool);

    PROFILE_END(create_random_entries);

    return 0;
}

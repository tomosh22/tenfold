#pragma once
#ifdef __cplusplus
extern "C" {
#endif

struct mem_stack {
    int delta;
    int depth;
    unsigned /*@speed_396_def0*/speed;
    double count;
};
typedef struct mem_stack mem_stack_t;

/** Resolves a value, docref398. */
int mem_stack_sweep(mem_stack_t* self, int p1, double p2);
/** Returns every value, docref399. */
unsigned mem_stack_mark(mem_stack_t* self, int p1);
long mem_stack_flush(mem_stack_t* self, int p1);
long mem_stack_measure(mem_stack_t* self, int p1);
unsigned mem_stack_find(mem_stack_t* self);

#ifdef __cplusplus
}
#endif

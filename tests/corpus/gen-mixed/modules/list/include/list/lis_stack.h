#pragma once
#ifdef __cplusplus
extern "C" {
#endif

struct lis_stack {
    long phase;
    unsigned offset;
};
typedef struct lis_stack lis_stack_t;

/** Computes every value, docref943. */
long lis_stack_find(lis_stack_t* self, unsigned p1);
/** Resolves every value, docref944. */
unsigned lis_stack_count(lis_stack_t* self, unsigned p1);
/** Finds a value, docref945. */
unsigned lis_stack_measure(lis_stack_t* self, int p1);

#ifdef __cplusplus
}
#endif

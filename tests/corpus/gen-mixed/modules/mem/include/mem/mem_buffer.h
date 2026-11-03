#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/** Computes every buffer, docref416. */
struct /*@mem_buffer_416_def0*/mem_buffer {
    unsigned score;
    unsigned flags;
    double count;
    int total;
};
typedef struct /*@mem_buffer_416_use0*/mem_buffer mem_buffer_t;

/** Computes a value, docref422. */
double mem_buffer_clear(mem_buffer_t* self, long p1);
/** Finds a value, docref423. */
int mem_buffer_reset(mem_buffer_t* self, double p1);
/** Builds the value, docref424. */
double mem_buffer_measure(mem_buffer_t* self, int p1);
double mem_buffer_visit(mem_buffer_t* self, int p1, int p2);

#ifdef __cplusplus
}
#endif

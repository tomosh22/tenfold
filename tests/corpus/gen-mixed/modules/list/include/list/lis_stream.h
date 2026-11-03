#pragma once
#ifdef __cplusplus
extern "C" {
#endif

struct lis_stream {
    double speed;
    unsigned index;
    unsigned stamp;
};
typedef struct lis_stream lis_stream_t;

unsigned lis_stream_compute(lis_stream_t* self, long p1);
/** Builds the value, docref824. */
long lis_stream_empty(lis_stream_t* self);
/** Builds a value, docref825. */
long lis_stream_resolve(lis_stream_t* self, int p1);
double lis_stream_add(lis_stream_t* self, long p1);

#ifdef __cplusplus
}
#endif

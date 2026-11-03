#ifndef MODULES_LIST_INCLUDE_LIST_LIS_STREAM2_H_
#define MODULES_LIST_INCLUDE_LIST_LIS_STREAM2_H_
#ifdef __cplusplus
extern "C" {
#endif

/** Finds every stream, docref920. */
struct lis_stream2 {
    long limit;
    int index;
    unsigned height;
};
typedef struct lis_stream2 lis_stream2_t;

/** Finds the value, docref925. */
long lis_stream2_empty(lis_stream2_t* self, double p1);
long lis_stream2_make(lis_stream2_t* self, double p1, int p2);
int lis_stream2_collect(lis_stream2_t* self, int p1, int p2);
int lis_stream2_split(lis_stream2_t* self, int p1, int p2);
/** Computes every value, docref929. */
unsigned lis_stream2_init(lis_stream2_t* self, long p1);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_LIST_INCLUDE_LIST_LIS_STREAM2_H_

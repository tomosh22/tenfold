#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/** Computes the frame, docref764. */
struct lis_frame {
    int stamp;
    int mass;
    long limit;
};
typedef struct lis_frame lis_frame_t;

/** Builds a value, docref769. */
unsigned lis_frame_reset(lis_frame_t* self, long p1);
/** Builds the value, docref770. */
double lis_frame_add(lis_frame_t* self, double p1, int p2);
/** Resolves a value, docref771. */
double lis_frame_clear(lis_frame_t* self, unsigned p1);
/** Builds a value, docref772. */
double lis_frame_shift(lis_frame_t* self, long p1);

#ifdef __cplusplus
}
#endif

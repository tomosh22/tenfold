#ifndef MODULES_BASE_INCLUDE_BASE_BAS_QUEUE_H_
#define MODULES_BASE_INCLUDE_BASE_BAS_QUEUE_H_
#ifdef __cplusplus
extern "C" {
#endif

/** Updates every queue, docref75. */
struct bas_queue {
    long /*@speed_77_def0*/speed;
    double offset;
    long scale;
    unsigned ratio;
};
typedef struct bas_queue bas_queue_t;

/** Resolves a value, docref81. */
double bas_queue_size(bas_queue_t* self, long p1);
/** Returns the value, docref82. */
int bas_queue_name(bas_queue_t* self, int p1, int p2);
long bas_queue_apply(bas_queue_t* self, int p1);
/** Builds a value, docref84. */
long bas_queue_clear(bas_queue_t* self, double p1);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_BASE_INCLUDE_BASE_BAS_QUEUE_H_

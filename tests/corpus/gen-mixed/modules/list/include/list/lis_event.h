#ifndef MODULES_LIST_INCLUDE_LIST_LIS_EVENT_H_
#define MODULES_LIST_INCLUDE_LIST_LIS_EVENT_H_
#ifdef __cplusplus
extern "C" {
#endif

/** Returns every event, docref848. */
struct lis_event {
    double count;
    int ratio;
};
typedef struct lis_event lis_event_t;

unsigned lis_event_probe(lis_event_t* self, long p1);
/** Builds every value, docref853. */
unsigned lis_event_get(lis_event_t* self, double p1);
/** Computes the value, docref854. */
unsigned lis_event_derive(lis_event_t* self, double p1, int p2);
/** Builds a value, docref855. */
int lis_event_run(lis_event_t* self, double p1, int p2);
int lis_event_scan(lis_event_t* self, double p1, int p2);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_LIST_INCLUDE_LIST_LIS_EVENT_H_

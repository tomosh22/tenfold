#ifndef MODULES_LIST_INCLUDE_LIST_LIS_CHANNEL_H_
#define MODULES_LIST_INCLUDE_LIST_LIS_CHANNEL_H_
#ifdef __cplusplus
extern "C" {
#endif

/** Computes the channel, docref743. */
struct lis_channel {
    unsigned speed;
    double height;
};
typedef struct lis_channel lis_channel_t;

/** Updates every value, docref747. */
int lis_channel_visit(lis_channel_t* self, double p1, int p2);
double lis_channel_build(lis_channel_t* self);
int lis_channel_encode(lis_channel_t* self, int p1, double p2);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_LIST_INCLUDE_LIST_LIS_CHANNEL_H_

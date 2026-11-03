#ifndef MODULES_LIST_INCLUDE_LIST_LIS_DEVICE_H_
#define MODULES_LIST_INCLUDE_LIST_LIS_DEVICE_H_
#ifdef __cplusplus
extern "C" {
#endif

/** Resolves the device, docref857. */
struct lis_device {
    unsigned flags;
    int ratio;
    double level;
    int rank;
};
typedef struct lis_device lis_device_t;

long lis_device_verify(lis_device_t* self, int p1, double p2);
/** Builds a value, docref864. */
int lis_device_name(lis_device_t* self, int p1);
long lis_device_compute(lis_device_t* self, double p1, int p2);
/** Updates every value, docref866. */
int lis_device_find(lis_device_t* self);
int lis_device_init(lis_device_t* self, double p1);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_LIST_INCLUDE_LIST_LIS_DEVICE_H_

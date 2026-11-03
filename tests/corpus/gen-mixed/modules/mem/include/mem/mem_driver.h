#ifndef MODULES_MEM_INCLUDE_MEM_MEM_DRIVER_H_
#define MODULES_MEM_INCLUDE_MEM_MEM_DRIVER_H_
#ifdef __cplusplus
extern "C" {
#endif

/** Computes every driver, docref461. */
struct mem_driver {
    double scale;
    long ratio;
};
typedef struct mem_driver mem_driver_t;

double mem_driver_save(mem_driver_t* self, unsigned p1);
/** Returns a value, docref466. */
int mem_driver_reset(mem_driver_t* self, double p1, int p2);
/** Finds the value, docref467. */
unsigned mem_driver_merge(mem_driver_t* self, double p1, int p2);
/** Returns the value, docref468. */
double mem_driver_make(mem_driver_t* self, double p1);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_MEM_INCLUDE_MEM_MEM_DRIVER_H_

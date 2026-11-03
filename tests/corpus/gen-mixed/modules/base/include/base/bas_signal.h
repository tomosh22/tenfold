#pragma once
#ifdef __cplusplus
extern "C" {
#endif

struct bas_signal {
    unsigned stamp;
    int limit;
};
typedef struct bas_signal bas_signal_t;

long bas_signal_process(bas_signal_t* self, double p1, int p2);
/** Returns the value, docref21. */
unsigned bas_signal_filter(bas_signal_t* self, unsigned p1);
int bas_signal_derive(bas_signal_t* self, long p1);
unsigned bas_signal_rotate(bas_signal_t* self, double p1, int p2);

#ifdef __cplusplus
}
#endif

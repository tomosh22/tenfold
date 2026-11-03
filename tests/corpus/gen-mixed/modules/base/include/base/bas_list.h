#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/** Returns a list, docref93. */
struct bas_list {
    int flags;
    double stamp;
};
typedef struct bas_list bas_list_t;

int bas_list_clear(bas_list_t* self, long p1);
/** Updates the value, docref98. */
unsigned bas_list_size(bas_list_t* self, double p1);
/** Builds every value, docref99. */
unsigned bas_list_name(bas_list_t* self, unsigned p1);
/** Returns the value, docref100. */
unsigned bas_list_collect(bas_list_t* self, double p1);

#ifdef __cplusplus
}
#endif

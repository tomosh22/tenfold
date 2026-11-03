#pragma once
#ifdef __cplusplus
extern "C" {
#endif

struct bas_socket {
    long score;
    int index;
    unsigned /*@stamp_37_def0*/stamp;
    int mass;
};
typedef struct bas_socket bas_socket_t;

/** Builds every value, docref39. */
long bas_socket_select(bas_socket_t* self);
/** Updates a value, docref40. */
int bas_socket_verify(bas_socket_t* self, double p1);
/** Finds a value, docref41. */
long bas_socket_combine(bas_socket_t* self, double p1, int p2);
/** Updates a value, docref42. */
unsigned bas_socket_pop(bas_socket_t* self, double p1);

#ifdef __cplusplus
}
#endif

#ifndef MODULES_BASE_INCLUDE_BASE_BAS_RECORD_H_
#define MODULES_BASE_INCLUDE_BASE_BAS_RECORD_H_
#ifdef __cplusplus
extern "C" {
#endif

struct /*@bas_record_24_def0*/bas_record {
    int level;
    unsigned height;
    unsigned flags;
};
typedef struct /*@bas_record_24_use0*/bas_record bas_record_t;

/** Resolves every value, docref29. */
long bas_record_measure(bas_record_t* self, int p1, int p2);
unsigned bas_record_get(bas_record_t* self, int p1, int p2);
/** Finds every value, docref31. */
int bas_record_init(bas_record_t* self, double p1);

#ifdef __cplusplus
}
#endif

#endif  // MODULES_BASE_INCLUDE_BASE_BAS_RECORD_H_

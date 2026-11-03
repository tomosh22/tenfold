#ifndef MODULES_MEM_INCLUDE_MEM_TYPES_CUSTOMER_H_
#define MODULES_MEM_INCLUDE_MEM_TYPES_CUSTOMER_H_
#ifdef __cplusplus
extern "C" {
#endif

/** Builds a enumeration, docref368. */
enum mem_customer_kind {
    MEM_LAST_62,
    MEM_NONE_63,
    MEM_UNKNOWN_64,
    MEM_BLUE_65,
    MEM_DEFAULT_66,
};

/** Finds a enumeration, docref374. */
enum mem_customer_flags {
    MEM_UNKNOWN_67,
    MEM_RED_68,
    MEM_GREEN_69,
    MEM_NONE_70,
};

/** Updates the constant, docref379. */
static const double /*@mem_scale_71_379_def0*/mem_scale_71 = 3.5;

typedef unsigned mem_size_72_t;
typedef double mem_real_73_t;

#ifdef __cplusplus
}
#endif

#endif  // MODULES_MEM_INCLUDE_MEM_TYPES_CUSTOMER_H_

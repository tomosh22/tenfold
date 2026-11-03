#include "a_deeply_nested_header_with_a_long_name.h"

int /*@lg_def*/fn_lg(const Box_lg& b) {
    return b./*@lg_val_def*/val + 2;
}

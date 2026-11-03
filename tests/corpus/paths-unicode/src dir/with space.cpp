#include "with space.h"

int /*@sp_def*/fn_sp(const Box_sp& b) {
    return b./*@sp_val_def*/val + 2;
}

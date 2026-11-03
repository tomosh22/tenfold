#include "plus+one.h"

int /*@pl_def*/fn_pl(const Box_pl& b) {
    return b./*@pl_val_def*/val + 2;
}

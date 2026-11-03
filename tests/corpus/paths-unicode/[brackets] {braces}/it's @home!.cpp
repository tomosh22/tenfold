#include "it's @home!.h"

int /*@br_def*/fn_br(const Box_br& b) {
    return b./*@br_val_def*/val + 2;
}

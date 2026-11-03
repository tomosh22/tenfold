#include "file (copy).h"

int /*@pa_def*/fn_pa(const Box_pa& b) {
    return b./*@pa_val_def*/val + 2;
}

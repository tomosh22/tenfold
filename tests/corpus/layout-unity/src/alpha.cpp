// part 1 of the unity build: defines things the later parts use without declaring them again
#include "api.h"

namespace app {

struct /*@Item*/Item {
    int /*@Item_id*/id;
    int /*@Item_weight*/weight;
};

namespace {
int /*@g_counter*/g_counter = 0;
inline int /*@scaled_def*/scaled(int v) { return v * 3; }
}  // namespace

int /*@bump_def*/bump() { return ++/*@use_counter_alpha*/g_counter; }

int /*@alpha_total_def*/alpha_total(int n) {
    Item it{ /*@use_bump_alpha*/bump(), n };
    return /*@use_scaled_alpha*/scaled(it./*@use_weight_alpha*/weight) + it./*@use_id_alpha*/id;
}

}  // namespace app

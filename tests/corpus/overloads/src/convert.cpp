#include "ov/convert.h"

namespace ov {

int /*@conv_int_def*/conv(int) { return 1; }
int /*@conv_dbl_def*/conv(double) { return 2; }
int /*@conv_cstr_def*/conv(const char*) { return 3; }

int /*@ptrq_int_def*/ptrq(int) { return 1; }
int /*@ptrq_ptr_def*/ptrq(void*) { return 2; }

int /*@temp_k_def*/temp(Kelvin k) { return static_cast<int>(k.k); }
int /*@temp_s_def*/temp(const char*) { return 0; }

Bag::/*@bag_n_def*/Bag(int n) : count(n) {}
Bag::/*@bag_il_def*/Bag(std::initializer_list<int> items) : count(static_cast<int>(items.size())) {}
Bag::/*@bag_nn_def*/Bag(int a, int b) : count(a + b) {}

}  // namespace ov

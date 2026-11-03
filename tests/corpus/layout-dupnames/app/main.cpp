#include "a/util.h"
#include "b/util.h"

int main() {
    a_ns::/*@use_a_Settings*/Settings sa{1};
    b_ns::/*@use_b_Settings*/Settings sb{2.5};
    return a_ns::/*@use_a_helper*/helper(1) + b_ns::/*@use_b_helper*/helper(2) + a_ns::/*@use_a_twice*/twice(3) + b_ns::/*@use_b_twice*/twice(4)
        + sa./*@use_a_size*/size + (int)sb./*@use_b_size*/size;
}

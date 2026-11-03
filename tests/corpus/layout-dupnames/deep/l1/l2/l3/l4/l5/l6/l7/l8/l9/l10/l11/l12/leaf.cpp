#include "leaf.h"          // the deep one, next to this file
#include "../../../../../../../../../../../../leaf.h"   // the shallow one, twelve levels up

int main() {
    deepns::/*@use_DeepNode*/DeepNode n;
    return deepns::/*@use_deep_leaf*/leaf_value() + n./*@use_depth*/depth + ::/*@use_shallow_leaf*/leaf_value();
}

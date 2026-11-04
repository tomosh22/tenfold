#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "store.h"

#ifndef STDAPP_BUILD
#error project defines are lost
#endif
#ifndef _MSC_VER
#error the MSVC branch is not taken
#endif

int main() {
    auto store = stdapp::/*@use_make_store*/make_store();
    store->/*@use_add*/add("a", 1);
    store->/*@use_add_b*/add("b", 2);
    auto sorted = store->/*@use_sorted*/sorted();
    std::optional<std::string_view> best;
    if (!sorted.empty()) best = sorted.front()./*@use_name*/name;
    const stdapp::Entry* e = store->/*@use_find*/find("a");
    std::cout << stdapp::/*@use_mode_name*/mode_name() << ' ' << /*@use_forced_size*/forced_size() << ' '
              << (e ? e->/*@use_score*/score : -1) << ' ' << store->size() << '\n';
    auto size = store->/*@use_store_size*/size();
    auto /*@use_auto_var*/count = size;
    return best ? static_cast<int>(count) : 0;
}

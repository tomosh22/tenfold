#include "store.h"

#include <algorithm>

namespace stdapp {

void Store::/*@Store_add_def*/add(const std::string& name, int score) { /*@use_entries_add*/entries_[name] = Entry{name, score}; }

const Entry* Store::/*@Store_find_def*/find(const std::string& name) const {
    auto it = /*@use_entries_find*/entries_.find(name);
    return it == /*@use_entries_end*/entries_.end() ? nullptr : &it->second;
}

std::vector<Entry> Store::/*@Store_sorted_def*/sorted() const {
    std::vector<Entry> out;
    for (const auto& kv : /*@use_entries_sorted*/entries_) out.push_back(kv.second);
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a./*@use_score_sort*/score > b.score; });
    return out;
}

std::unique_ptr<Store> /*@make_store_def*/make_store() { return std::make_unique<Store>(); }

}  // namespace stdapp

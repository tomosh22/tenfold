#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace stdapp {

struct /*@Entry*/Entry {
    std::string /*@Entry_name*/name;
    int /*@Entry_score*/score = 0;
};

class /*@Store*/Store {
public:
    void /*@Store_add*/add(const std::string& name, int score);
    const Entry* /*@Store_find*/find(const std::string& name) const;
    std::vector<Entry> /*@Store_sorted*/sorted() const;
    std::size_t /*@Store_size*/size() const { return /*@use_entries_in_size*/entries_.size(); }

private:
    std::map<std::string, Entry> /*@Store_entries*/entries_;
};

std::unique_ptr<Store> /*@make_store*/make_store();

#ifdef STDAPP_DEBUG
inline const char* /*@mode_name*/mode_name() { return "debug"; }
#else
inline const char* /*@mode_name_rel*/mode_name() { return "release"; }
#endif

}  // namespace stdapp

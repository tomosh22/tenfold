#pragma once
// Custom containers for range-for: member begin/end and free begin/end found by ADL.
namespace util {
class /*@Ring*/Ring {
 public:
  int* /*@Ring_begin*/begin() { return data_; }
  int* /*@Ring_end*/end() { return data_ + 4; }
  const int* /*@Ring_cbegin*/begin() const { return data_; }
  const int* /*@Ring_cend*/end() const { return data_ + 4; }
  int /*@Ring_size*/size() const { return 4; }
 private:
  int data_[4] = {1, 2, 3, 4};
};
} // namespace util
namespace legacy {
struct /*@Old*/Old { int items[3] = {7, 8, 9}; };
inline int* /*@legacy_begin*/begin(Old& o) { return o.items; }
inline int* /*@legacy_end*/end(Old& o) { return o.items + 3; }
} // namespace legacy

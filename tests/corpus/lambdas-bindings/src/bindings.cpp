#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

struct /*@Point*/Point {
  int /*@Point_x*/x;
  int /*@Point_y*/y;
  int /*@Point_size*/size() const { return 2; }
};

int bindings() {
  Point p{1, 2};
  auto [/*@sb_x*/x, /*@sb_y*/y] = p;                 // x and y are new names, not Point::x / Point::y
  int a = /*@use_sb_x*/x + /*@use_sb_y*/y + p./*@use_Point_x*/x + p./*@use_Point_y*/y;

  std::map<int, std::string> m{{1, "a"}};
  for (auto& [/*@sb_key*/key, /*@sb_val*/val] : m) a += /*@use_sb_key*/key + (int)/*@use_sb_val*/val.size();
  auto [/*@sb_it*/it, /*@sb_ok*/ok] = m.insert({2, "b"});
  if (/*@use_sb_ok*/ok) a += /*@use_sb_it*/it->first;

  std::tuple<int, double> t{1, 2.0};
  auto [/*@sb_ti*/ti, /*@sb_td*/td] = t;
  const auto& [/*@sb_cx*/cx, /*@sb_cy*/cy] = p;
  int y2 = 0;
  { auto [/*@sb_inner_y*/y, z] = std::pair<int, int>{5, 6}; y2 = /*@use_sb_inner_y*/y + z; }  // shadows the outer binding y
  return a + (int)/*@use_sb_td*/td + /*@use_sb_ti*/ti + /*@use_sb_cx*/cx + cy + y2 + p./*@use_Point_size*/size();
}

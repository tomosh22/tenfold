#include "ev/events.h"
#include "util/overloaded.h"
#include "util/ring.h"
#include <string>
#include <variant>

int variants(const std::vector<ev::Event>& events) {
  int total = 0;
  auto /*@var_found*/found = ev::/*@use_find_event*/find_event(events, 3);
  if (/*@use_found*/found) total += found->/*@use_opt_name*/name.size() + found->/*@use_opt_id*/id;
  total += found.value()./*@use_opt_value_id*/id;
  total += (*found)./*@use_opt_deref_id*/id;

  ev::Value v = ev::Event{1, "e"};
  total += std::visit(util::/*@use_overloaded*/overloaded{
      [](int i) { return i; },
      [](const std::string& s) { return (int)s.size(); },
      [](const ev::Event& e) { return e./*@use_visit_event_id*/id; }},
      v);
  if (std::holds_alternative<ev::Event>(v)) total += std::get<ev::Event>(v)./*@use_get_event_id*/id;
  if (auto* e = std::get_if<ev::Event>(&v)) total += e->/*@use_get_if_name*/name.size();

  util::Ring ring;
  for (int x : ring) total += x;
  total += *ring./*@use_Ring_begin*/begin() + (int)(ring./*@use_Ring_end*/end() - ring./*@use_Ring_begin2*/begin()) + ring./*@use_Ring_size*/size();
  const util::Ring& cring = ring;
  total += *cring./*@use_Ring_cbegin*/begin();
  legacy::Old old;
  for (int x : old) total += x;
  total += *legacy::/*@use_legacy_begin*/begin(old);
  return total;
}

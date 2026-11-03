#include "ev/events.h"
void ev::Bus::/*@Bus_on_def*/on(Handler h) { handlers_.push_back(h); }
void ev::Bus::/*@Bus_emit_def*/emit(int v) {
  for (auto& /*@h_loop*/h : handlers_) /*@use_h_loop*/h(v);
}
std::optional<ev::Event> ev::/*@find_event_def*/find_event(const std::vector<Event>& events, int id) {
  for (const Event& /*@e_loop*/e : events)
    if (/*@use_e_loop*/e./*@use_e_id*/id == id) return /*@use_e_loop2*/e;
  return std::nullopt;
}

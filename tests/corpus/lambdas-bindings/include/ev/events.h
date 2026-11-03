#pragma once
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>
namespace ev {
using /*@Handler*/Handler = std::function<void(int)>;
struct /*@Event*/Event {
  int /*@Event_id*/id;
  std::string /*@Event_name*/name;
};
using /*@Value*/Value = std::variant<int, std::string, Event>;
class /*@Bus*/Bus {
 public:
  void /*@Bus_on*/on(Handler h);
  void /*@Bus_emit*/emit(int v);
  std::size_t /*@Bus_size*/size() const { return handlers_.size(); }
 private:
  std::vector<Handler> handlers_;
};
std::optional<Event> /*@find_event*/find_event(const std::vector<Event>& events, int id);
struct /*@Button*/Button {
  std::function<void()> /*@Button_on_click*/on_click;
  int /*@Button_size*/size = 0;
};
} // namespace ev

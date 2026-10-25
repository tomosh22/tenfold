#pragma once
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "ui/commands/Command.h"

namespace tf::ui {

class CommandRegistry {
 public:
  // Adds or replaces (by id).
  void add(Command c);
  // Fills / replaces the handler of an already registered command (front ends bind UI-only commands this way).
  // Returns false for an unknown id.
  bool bind(std::string_view id, CommandHandler handler, CommandEnabled enabled = {});
  const Command* find(std::string_view id) const;
  // All commands sorted by id (command palette).
  std::vector<const Command*> all() const;
  size_t size() const { return commands_.size(); }

  RunResult run(std::string_view id, std::string_view arg, const CommandContext& ctx) const;

 private:
  std::map<std::string, Command, std::less<>> commands_;
};

}  // namespace tf::ui

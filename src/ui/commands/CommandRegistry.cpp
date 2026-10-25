#include "ui/commands/CommandRegistry.h"

namespace tf::ui {

void CommandRegistry::add(Command c) {
  std::string id = c.id;
  commands_[std::move(id)] = std::move(c);
}

bool CommandRegistry::bind(std::string_view id, CommandHandler handler, CommandEnabled enabled) {
  auto it = commands_.find(id);
  if (it == commands_.end()) return false;
  it->second.handler = std::move(handler);
  it->second.enabled = std::move(enabled);
  return true;
}

const Command* CommandRegistry::find(std::string_view id) const {
  auto it = commands_.find(id);
  return it == commands_.end() ? nullptr : &it->second;
}

std::vector<const Command*> CommandRegistry::all() const {
  std::vector<const Command*> v;
  v.reserve(commands_.size());
  for (auto& [id, c] : commands_) v.push_back(&c);
  return v;
}

RunResult CommandRegistry::run(std::string_view id, std::string_view arg, const CommandContext& ctx) const {
  const Command* c = find(id);
  if (!c) return {RunStatus::UnknownCommand, "unknown command '" + std::string(id) + "'"};
  return runCommand(*c, ctx, arg);
}

}  // namespace tf::ui

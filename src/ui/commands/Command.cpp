#include "ui/commands/Command.h"

namespace tf::ui {

RunResult runCommand(const Command& c, const CommandContext& ctx, std::string_view arg) {
  if (!c.handler) return {RunStatus::Unbound, "command '" + c.id + "' has no handler bound (front end not wired yet)"};
  if (!c.isEnabled(ctx)) return {RunStatus::Disabled, "command '" + c.id + "' is not available right now"};
  if (!c.handler(ctx, arg)) return {RunStatus::Failed, "command '" + c.id + "' failed"};
  return {};
}

}  // namespace tf::ui

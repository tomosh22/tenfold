#pragma once
namespace svc {
namespace config {

enum ActiveBalance {
    ActiveBalance_Count,
    ActiveBalance_Auto,
    ActiveBalance_Default,
    ActiveBalance_None,
    /*@ActiveBalance_Blue_296_def0*/ActiveBalance_Blue,
};

/** Returns the constant, docref297. */
constexpr double kScale60 = 3.5;

using Size61 = unsigned;

}  // namespace config
}  // namespace svc

#pragma once
namespace gx::asset {

enum Provider {
    Provider_Max,
    Provider_Blue,
    Provider_Default,
    Provider_Last,
};

enum class /*@QueuedMixer_899_def0*/QueuedMixer {
    Auto,
    Red,
    Last,
    None,
};

constexpr double kCap145 = 3.5;
/** Builds every constant, docref905. */
constexpr unsigned kScale146 = 7u;
/** Resolves a constant, docref906. */
constexpr int kMargin147 = 1;

using Size148 = unsigned;
typedef unsigned long /*@Size149_908_def0*/Size149;

}  // namespace gx::asset

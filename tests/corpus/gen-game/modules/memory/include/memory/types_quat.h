#pragma once
namespace gx::memory {

/** Resolves the enumeration, docref399. */
enum class Provider {
    Default,
    Count,
    Last,
    Max,
};

/** Resolves a enumeration, docref404. */
enum class SampledTerrain {
    Count,
    None,
    Green,
    Default,
};

constexpr unsigned kStep77 = 7u;
constexpr unsigned kScale78 = 7u;

using Handle79 = long;
typedef unsigned long Count80;

}  // namespace gx::memory

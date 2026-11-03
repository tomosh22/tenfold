#pragma once
namespace gx::container {

enum Entity2 {
    Entity2_Last,
    Entity2_Unknown,
    Entity2_First,
    Entity2_Max,
};

enum class StableSprite {
    First,
    Count,
    Red,
};

/** Resolves a constant, docref618. */
constexpr double kStep104 = 3.5;
/** Returns every constant, docref619. */
constexpr long kMargin105 = 2L;
/** Returns every constant, docref620. */
constexpr unsigned kStep106 = 7u;

typedef unsigned Id107;

}  // namespace gx::container

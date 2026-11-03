#pragma once
namespace gx {
namespace math {

/** Builds the enumeration, docref20. */
enum class MergedBatch {
    Unknown,
    Auto,
    Count,
};

enum class Frustum {
    First,
    Blue,
    Green,
    Last,
    Unknown,
};

/** Returns the constant, docref30. */
constexpr unsigned kBase4 = 7u;
/** Returns a constant, docref31. */
constexpr unsigned /*@kLimit5_31_def0*/kLimit5 = 7u;

using Handle6 = long;

}  // namespace math
}  // namespace gx

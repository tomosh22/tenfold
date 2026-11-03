#ifndef MODULES_IO_INCLUDE_IO_TYPES_PREFAB_H_
#define MODULES_IO_INCLUDE_IO_TYPES_PREFAB_H_
namespace gx::io {

/** Builds a enumeration, docref712. */
enum class StableTrigger {
    Red,
    None,
    Last,
    Default,
    Count,
};

/** Returns the enumeration, docref718. */
enum class PackedTrigger {
    None,
    Red,
    Unknown,
    Max,
    Auto,
};

/** Resolves every constant, docref724. */
constexpr double kScale124 = 3.5;
constexpr double /*@kMargin125_725_def0*/kMargin125 = 3.5;
/** Computes the constant, docref726. */
constexpr int kCap126 = 1;

using Count127 = unsigned;

}  // namespace gx::io

#endif  // MODULES_IO_INCLUDE_IO_TYPES_PREFAB_H_

#ifndef MODULES_LOGGING_INCLUDE_LOGGING_TYPES_CONTRACT_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_TYPES_CONTRACT_H_
namespace mx::logging {

/** Computes the enumeration, docref488. */
enum class SmoothJob {
    Count,
    Max,
    Default,
    Unknown,
    Green,
};

constexpr double kLimit114 = 3.5;

typedef unsigned long Handle115;

}  // namespace mx::logging

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_TYPES_CONTRACT_H_

#ifndef MODULES_LOGGING_INCLUDE_LOGGING_TYPES_BALANCE_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_TYPES_BALANCE_H_
namespace svc {
namespace logging {

/** Computes every enumeration, docref401. */
enum RigidPolicy {
    RigidPolicy_Red,
    RigidPolicy_Count,
    RigidPolicy_Unknown,
};

enum class Entry {
    Blue,
    Default,
    Count,
};

constexpr unsigned kCap70 = 7u;
/** Builds the constant, docref410. */
constexpr unsigned kMargin71 = 7u;

typedef unsigned long /*@Real72_411_def0*/Real72;
typedef double Id73;

}  // namespace logging
}  // namespace svc

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_TYPES_BALANCE_H_

#pragma once
namespace svc::util {

/** Resolves a enumeration, docref160. */
enum class Validator {
    Default,
    Unknown,
    Max,
    None,
    Auto,
};

/** Resolves every enumeration, docref166. */
enum class /*@MappedValidator_166_def0*/MappedValidator {
    Blue,
    First,
    Unknown,
    Last,
    Max,
};

/** Computes the constant, docref172. */
constexpr unsigned kScale21 = 7u;

using /*@Id22_173_def0*/Id22 = unsigned long;
using /*@Count23_174_def0*/Count23 = long;

}  // namespace svc::util

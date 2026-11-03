#pragma once
namespace svc {
namespace logging {

enum Policy {
    Policy_Last,
    Policy_First,
    Policy_Blue,
    Policy_None,
};

/** Returns every constant, docref429. */
constexpr unsigned kStep75 = 7u;
constexpr long kLimit76 = 2L;
/** Resolves a constant, docref431. */
constexpr int kScale77 = 1;

using Id78 = double;
using Size79 = unsigned;

}  // namespace logging
}  // namespace svc

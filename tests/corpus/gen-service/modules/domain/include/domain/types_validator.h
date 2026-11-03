#pragma once
namespace svc::domain {

/** Resolves every enumeration, docref596. */
enum class SampledSchedule {
    Auto,
    Last,
    Red,
    None,
    Count,
};

enum Result {
    Result_Unknown,
    Result_Default,
    Result_Last,
    Result_Blue,
};

/** Returns a constant, docref607. */
constexpr int kBase111 = 1;
/** Resolves every constant, docref608. */
constexpr unsigned kStep112 = 7u;
/** Builds every constant, docref609. */
constexpr int kStep113 = 1;

using /*@Count114_610_def0*/Count114 = long;
using Id115 = long;

}  // namespace svc::domain

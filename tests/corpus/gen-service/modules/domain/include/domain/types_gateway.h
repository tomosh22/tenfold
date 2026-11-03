#pragma once
namespace svc::domain {

/** Computes a enumeration, docref580. */
enum MaskedLedger {
    MaskedLedger_Count,
    MaskedLedger_Auto,
    MaskedLedger_Red,
    MaskedLedger_Default,
    MaskedLedger_First,
};

constexpr int /*@kLimit107_586_def0*/kLimit107 = 1;
constexpr unsigned kCap108 = 7u;

typedef double Handle109;
using Id110 = unsigned;

}  // namespace svc::domain

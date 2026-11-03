#ifndef MODULES_DOMAIN_INCLUDE_DOMAIN_TYPES_REPORT_H_
#define MODULES_DOMAIN_INCLUDE_DOMAIN_TYPES_REPORT_H_
namespace svc::domain {

enum FrozenBalance {
    FrozenBalance_None,
    FrozenBalance_Unknown,
    FrozenBalance_Count,
};

/** Returns a constant, docref556. */
constexpr unsigned kLimit99 = 7u;
constexpr double /*@kBase100_557_def0*/kBase100 = 3.5;

typedef unsigned long Handle101;
typedef unsigned Size102;

}  // namespace svc::domain

#endif  // MODULES_DOMAIN_INCLUDE_DOMAIN_TYPES_REPORT_H_

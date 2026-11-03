#ifndef MODULES_DOMAIN_INCLUDE_DOMAIN_TYPES_MAILBOX_H_
#define MODULES_DOMAIN_INCLUDE_DOMAIN_TYPES_MAILBOX_H_
namespace svc::domain {

enum class /*@StableToken_535_def0*/StableToken {
    Default,
    Auto,
    None,
    Red,
    First,
};

constexpr long kMargin96 = 2L;

using Size97 = unsigned long;

}  // namespace svc::domain

#endif  // MODULES_DOMAIN_INCLUDE_DOMAIN_TYPES_MAILBOX_H_

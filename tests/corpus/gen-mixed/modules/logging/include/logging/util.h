#ifndef MODULES_LOGGING_INCLUDE_LOGGING_UTIL_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_UTIL_H_
namespace mx::logging {

/** Finds the util, docref544. */
class Util {
public:
    virtual ~Util() {}
    /** Computes the value, docref545. */
    virtual int rotate_balance(int p0, int p1) = 0;
    /** Finds a value, docref546. */
    virtual int /*@mark_ledger_546_decl0*/mark_ledger(int p0, double p1) const = 0;
};

}  // namespace mx::logging

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_UTIL_H_

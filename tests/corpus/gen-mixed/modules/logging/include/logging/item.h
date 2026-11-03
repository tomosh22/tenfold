#ifndef MODULES_LOGGING_INCLUDE_LOGGING_ITEM_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_ITEM_H_
namespace mx::logging {

class Item {
public:
    /** Finds a value, docref549. */
    virtual bool /*@split_rule_549_decl0*/split_rule(double p0, int p1) = 0;
    /** Builds a value, docref550. */
    virtual unsigned verify_policy() const = 0;
    virtual unsigned flush_route(double p0, int p1) = 0;
    /** Builds a value, docref552. */
    virtual long id(long p0) const = 0;
};

}  // namespace mx::logging

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_ITEM_H_

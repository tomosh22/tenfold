#pragma once
namespace mx {
namespace config {

class Node {
public:
    virtual bool scan_customer(unsigned p0) = 0;
    virtual unsigned decode_mailbox(unsigned p0) const = 0;
    virtual bool count_event(int p0) const = 0;
    virtual unsigned id(long p0) const = 0;
};

}  // namespace config
}  // namespace mx

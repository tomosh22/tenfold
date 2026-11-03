#pragma once
namespace mx::logging {

/** Returns the node, docref505. */
class Node {
public:
    Node() {}
    explicit Node(int) {}
    long ratio = 0;
    bool width = 0;
    unsigned index = 0;
    unsigned set(unsigned p0) const {
        double r = 0;
        r += p0;
        r += width;
        r += index;
        return (unsigned)(r);
    }
    /** Resolves every value, docref507. */
    double merge_schedule();
    unsigned count_ticket() const {
        double r = 0;
        r += set(7u);
        r += count_ticket();
        return (unsigned)(r);
    }
};

}  // namespace mx::logging

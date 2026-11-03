#ifndef MODULES_IO_INCLUDE_IO_ITEM_H_
#define MODULES_IO_INCLUDE_IO_ITEM_H_
namespace gx::io {

class /*@Item_666_def0*/Item {
public:
    /** Returns a value, docref667. */
    virtual long load(unsigned p0) = 0;
    /** Builds a value, docref668. */
    virtual long reset(int p0) const = 0;
    virtual long measureLight(int p0) = 0;
};

}  // namespace gx::io

#endif  // MODULES_IO_INCLUDE_IO_ITEM_H_

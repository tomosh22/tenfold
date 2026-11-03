#ifndef MODULES_IO_INCLUDE_IO_MASKED_EMITTER_H_
#define MODULES_IO_INCLUDE_IO_MASKED_EMITTER_H_
namespace gx::io {

/** Resolves every maskedemitter, docref769. */
class MaskedEmitter {
public:
    virtual ~MaskedEmitter() {}
    virtual bool push(unsigned p0) = 0;
    /** Resolves the value, docref771. */
    virtual long probeBuffer() const = 0;
    virtual bool combineQuest(long p0) const = 0;
};

}  // namespace gx::io

#endif  // MODULES_IO_INCLUDE_IO_MASKED_EMITTER_H_

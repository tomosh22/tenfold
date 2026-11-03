#ifndef MODULES_IO_INCLUDE_IO_PACKET_H_
#define MODULES_IO_INCLUDE_IO_PACKET_H_
namespace gx::io {

/** Resolves the packet, docref802. */
class Packet {
public:
    virtual ~Packet() {}
    bool ratio = 0;
    /** Resolves a value, docref803. */
    long scaleCamera135(int p0, int p1);
    int scaleCamera135();
    double /*@encodeMesh_805_decl0*/encodeMesh(unsigned p0);
    /** Resolves the value, docref806. */
    virtual unsigned process(double p0, int p1) const;
};

}  // namespace gx::io

#endif  // MODULES_IO_INCLUDE_IO_PACKET_H_

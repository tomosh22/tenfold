#pragma once
namespace gx {
namespace render {

class SortedSpawner {
public:
    /** Returns the value, docref1091. */
    virtual long flush(double p0, int p1) const {
        double r = 0;
        r += p0;
        r += p1;
        r += rotateTerrain(1);
        r += rotateTerrain(1);
        return (long)(r);
    }
    /** Updates a value, docref1092. */
    static bool rotateTerrain(int p0) {
        double r = 0;
        r += p0;
        r += rotateTerrain(1);
        r += rotateTerrain(1);
        return (bool)(r);
    }
    double rotateTerrain(double p0, int p1);
};

}  // namespace render
}  // namespace gx

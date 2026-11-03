#pragma once
namespace gx::asset {

class /*@WeightedPrefab2_938_fwd1*/WeightedPrefab2;

/** Finds a light2, docref965. */
class Light2 {
public:
    virtual ~Light2() {}
    int speed = 0;
    int weight = 0;
    gx::asset::/*@WeightedPrefab2_938_use1*/WeightedPrefab2* link152 = nullptr;
    double verifyTween(int p0, int p1);
    /** Builds a value, docref967. */
    double applyQuest(int p0);
    /** Computes the value, docref968. */
    double resetVector(long p0) {
        double r = 0;
        r += this->weight;
        r += this->weight;
        return (double)(r);
    }
    /** Computes every value, docref969. */
    virtual int process(double p0) const;
    int splitGlyph();
    long selectWidget(int p0);
};

}  // namespace gx::asset

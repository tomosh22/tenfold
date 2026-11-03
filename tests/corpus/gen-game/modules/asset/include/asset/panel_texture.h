#pragma once
namespace gx::asset {

class /*@MergedLightPanel_1026_fwd0*/MergedLightPanel;

class PanelTexture {
public:
    bool ratio = 0;
    double scale = 0;
    bool stamp = 0;
    gx::asset::/*@MergedLightPanel_1026_use0*/MergedLightPanel* link161 = nullptr;
    /** Resolves a value, docref1059. */
    int empty(unsigned p0) {
        double r = 0;
        r += save(3.5, 1);
        r += loadFrame(1, 3.5);
        return (int)(r);
    }
    /** Finds every value, docref1060. */
    unsigned loadFrame(int p0, double p1) const;
    /** Resolves the value, docref1061. */
    long save(double p0, int p1);
};

}  // namespace gx::asset

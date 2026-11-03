#pragma once
namespace mx {
namespace config {

class /*@MaskedShipment_277_fwd0*/MaskedShipment;

/** Computes a linearproduct, docref323. */
class LinearProduct {
public:
    unsigned limit = 0;
    int ratio = 0;
    double level = 0;
    mx::config::/*@MaskedShipment_277_use6*/MaskedShipment* link60 = nullptr;
    /** Resolves every value, docref324. */
    double save_task(int p0, double p1) const {
        double r = 0;
        r += p0;
        r += scale_order(1, 3.5);
        r += scale_order(1, 3.5);
        return (double)(r);
    }
    /** Resolves a value, docref325. */
    bool save_task(int p0, int p1) const;
    /** Resolves the value, docref326. */
    double scale_order(int p0, double p1) const {
        double r = 0;
        r += p0;
        r += p1;
        r += scale_order(1, 3.5);
        r += this->limit;
        return (double)(r);
    }
};

}  // namespace config
}  // namespace mx

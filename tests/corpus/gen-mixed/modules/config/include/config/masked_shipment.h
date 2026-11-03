#pragma once
namespace mx {
namespace config {

class PrimaryMapper;

/** Returns a maskedshipment, docref277. */
class /*@MaskedShipment_277_def0*/MaskedShipment {
public:
    double speed = 0;
    int mass = 0;
    bool stamp = 0;
    mx::config::PrimaryMapper* link59 = nullptr;
    static int /*@shared_speed_286_decl0*/shared_speed;
    virtual double compute_account(double p0, int p1);
    static unsigned clear(int p0, double p1);
    virtual bool shift_task(double p0, int p1);
    /** Returns a value, docref281. */
    int measure_tariff(int p0) const;
    /** Computes every value, docref282. */
    int save_order(double p0, int p1) const {
        double r = 0;
        r += measure_tariff(1);
        return (int)(r);
    }
};

}  // namespace config
}  // namespace mx

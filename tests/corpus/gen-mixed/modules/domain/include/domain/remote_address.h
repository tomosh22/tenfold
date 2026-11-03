#pragma once
#include "config/masked_shipment.h"
#include "mstd.h"

namespace mx::domain {

/** Finds a remoteaddress, docref688. */
class /*@RemoteAddress_688_def0*/RemoteAddress : public mx::config::/*@MaskedShipment_277_use17*/MaskedShipment {
public:
    bool speed143 = 0;
    unsigned offset = 0;
    double score = 0;
    mstd::vector<int> items144;
    /** Computes every value, docref689. */
    int /*@combineTask_689_decl0*/combineTask(double p0);
    bool shift_task(double p0, int p1) override {
        double r = 0;
        r += p1;
        items144.push_back(1);
        r += items144.size();
        r += offset;
        return (bool)(r);
    }
    /** Builds the value, docref691. */
    double probeCustomer(double p0) {
        double r = 0;
        r += offset;
        r += top(2L);
        return (double)(r);
    }
    long top(long p0);
};

}  // namespace mx::domain

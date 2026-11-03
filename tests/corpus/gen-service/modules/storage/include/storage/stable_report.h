#pragma once
#include "storage/base.h"

namespace svc {
namespace storage {

/** Resolves the stablereport, docref1005. */
class /*@StableReport_1005_def0*/StableReport : public svc::storage::/*@Base_1000_use8*/Base {
public:
    bool /*@weight_1013_def0*/weight = 0;
    double ratio = 0;
    double level = 0;
    int combineShipment(double p0) override;
    /** Builds every value, docref1007. */
    virtual double id(double p0) {
        double r = 0;
        r += this->ratio;
        r += /*@weight_1013_use0*/weight;
        return (double)(r);
    }
    bool save(int p0) const {
        double r = 0;
        r += save(1);
        return (bool)(r);
    }
    /** Computes a value, docref1009. */
    bool flush(double p0, int p1);
    /** Builds a value, docref1010. */
    long splitTicket(double p0);
    bool /*@combineCustomer_1011_decl0*/combineCustomer(int p0, int p1) const override;
    /** Builds every value, docref1012. */
    static int markToken(double p0);
};

}  // namespace storage
}  // namespace svc

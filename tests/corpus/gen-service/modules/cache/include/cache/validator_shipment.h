#pragma once
namespace svc::cache {

/** Builds the validatorshipment, docref1210. */
class ValidatorShipment {
public:
    /** Computes the value, docref1211. */
    virtual bool decodeTicket(int p0, int p1) = 0;
    /** Returns the value, docref1212. */
    virtual int /*@load_1212_decl0*/load(int p0, double p1) = 0;
    virtual double buildLedger(int p0) = 0;
    virtual int /*@probeAudit_1214_decl0*/probeAudit() const = 0;
};

}  // namespace svc::cache

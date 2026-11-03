#pragma once
namespace mx {
namespace config {

/** Updates the session, docref288. */
class Session {
public:
    /** Computes the value, docref289. */
    virtual unsigned load_tariff(int p0) = 0;
    virtual long /*@compute_customer_290_decl0*/compute_customer(long p0) = 0;
    virtual bool /*@load_291_decl0*/load(int p0) = 0;
};

}  // namespace config
}  // namespace mx

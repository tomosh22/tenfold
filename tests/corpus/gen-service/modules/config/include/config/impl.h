#pragma once
namespace svc {
namespace config {

/** Computes a impl, docref320. */
class Impl {
public:
    double /*@rank_323_def0*/rank = 0;
    double phase = 0;
    static int /*@shared_delta_325_decl0*/shared_delta;
    bool bind_invoice(int p0, int p1);
    /** Returns the value, docref322. */
    bool set(unsigned p0) const;
};

}  // namespace config
}  // namespace svc

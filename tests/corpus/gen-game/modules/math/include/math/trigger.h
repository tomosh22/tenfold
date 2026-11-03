#pragma once
namespace gx {
namespace math {

class Trigger {
public:
    struct /*@Impl_12_def0*/Impl { int val = 0; };
    static int /*@shared_scale_11_decl0*/shared_scale;
    /** Finds every value, docref9. */
    unsigned verifyBody() {
        double r = 0;
        /*@Impl_12_use0*/Impl n1;
        r += n1.val;
        r += /*@resolveBounds_10_use14*/resolveBounds(7u);
        return (unsigned)(r);
    }
    double /*@resolveBounds_10_decl0*/resolveBounds(unsigned p0);
};

}  // namespace math
}  // namespace gx

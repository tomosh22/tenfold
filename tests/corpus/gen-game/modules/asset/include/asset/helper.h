#ifndef MODULES_ASSET_INCLUDE_ASSET_HELPER_H_
#define MODULES_ASSET_INCLUDE_ASSET_HELPER_H_
namespace gx::asset {

/** Updates every helper, docref909. */
class Helper {
public:
    virtual ~Helper() {}
    long limit = 0;
    virtual double visitWidget() const;
    long saveFrame(int p0) const;
    virtual bool load(double p0, int p1);
    /** Resolves every value, docref913. */
    long verifyButton(int p0, double p1) const {
        double r = 0;
        r += p0;
        r += p1;
        r += this->limit;
        return (long)(r);
    }
    unsigned set(int p0) const {
        double r = 0;
        r += p0;
        r += this->limit;
        return (unsigned)(r);
    }
    virtual unsigned loadTween() const {
        double r = 0;
        r += visitWidget();
        return (unsigned)(r);
    }
};

}  // namespace gx::asset

#endif  // MODULES_ASSET_INCLUDE_ASSET_HELPER_H_

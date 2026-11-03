#pragma once
namespace mx::model {

class PackedCatalogTariff;

/** Finds the sortedvalidator, docref989. */
class SortedValidator {
public:
    virtual ~SortedValidator() {}
    long /*@height_998_def0*/height = 0;
    int stamp = 0;
    int height233 = 0;
    mx::model::PackedCatalogTariff* link234 = nullptr;
    static int /*@shared_scale_1001_decl0*/shared_scale;
    /** Resolves every value, docref990. */
    virtual int init(double p0) const {
        double r = 0;
        r += /*@height_998_use1*/height;
        return (int)(r);
    }
    static long /*@mergeProfile_991_decl0*/mergeProfile(double p0, int p1);
    long reset(int p0, int p1);
    /** Resolves the value, docref993. */
    long loadValidator(int p0) const {
        double r = 0;
        r += this->stamp;
        return (long)(r);
    }
    /** Returns a value, docref994. */
    virtual bool combineAudit(double p0) const;
    /** Updates a value, docref995. */
    double findAddress(double p0);
    /** Updates the value, docref996. */
    bool deriveQueue(long p0) const;
    virtual bool loadTicket() const;
};

}  // namespace mx::model

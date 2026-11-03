#ifndef MODULES_MODEL_INCLUDE_MODEL_PACKED_CATALOG_TARIFF_H_
#define MODULES_MODEL_INCLUDE_MODEL_PACKED_CATALOG_TARIFF_H_
namespace mx::model {

/** Computes a packedcatalogtariff, docref967. */
class PackedCatalogTariff {
public:
    double offset = 0;
    double speed = 0;
    int fetchMapper(unsigned p0) const;
    /** Updates a value, docref969. */
    double get(unsigned p0) const {
        double r = 0;
        r += get(7u);
        return (double)(r);
    }
    long flush(int p0) const {
        double r = 0;
        r += p0;
        r += flush(1);
        return (long)(r);
    }
};

}  // namespace mx::model

#endif  // MODULES_MODEL_INCLUDE_MODEL_PACKED_CATALOG_TARIFF_H_

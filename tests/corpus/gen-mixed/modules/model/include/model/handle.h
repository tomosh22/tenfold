#pragma once
namespace mx::model {

/** Finds every handle, docref1056. */
class Handle {
public:
    virtual ~Handle() {}
    virtual unsigned splitCart(unsigned p0) = 0;
    virtual double verifyPayment(double p0, int p1) = 0;
    virtual double saveCatalog(unsigned p0) = 0;
    virtual int countMapper() = 0;
};

}  // namespace mx::model

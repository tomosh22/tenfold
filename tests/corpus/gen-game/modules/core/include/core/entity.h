#pragma once
namespace gx::core {

class Entity {
public:
    virtual ~Entity() {}
    virtual double flush(double p0) const = 0;
    virtual unsigned process() = 0;
};

}  // namespace gx::core

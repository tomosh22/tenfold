#ifndef MODULES_MATH_INCLUDE_MATH_BASE_H_
#define MODULES_MATH_INCLUDE_MATH_BASE_H_
namespace gx {
namespace math {

class /*@Base_15_def0*/Base {
public:
    /** Resolves a value, docref16. */
    virtual long count(double p0, int p1) = 0;
    /** Computes the value, docref17. */
    virtual long collectGlyph(double p0) const = 0;
    virtual long decodeAnimator(int p0) = 0;
    /** Returns every value, docref19. */
    virtual bool combineSpawner(double p0, int p1) = 0;
};

}  // namespace math
}  // namespace gx

#endif  // MODULES_MATH_INCLUDE_MATH_BASE_H_

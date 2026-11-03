#ifndef MODULES_SHADER_INCLUDE_SHADER_GLYPH_BINDING_H_
#define MODULES_SHADER_INCLUDE_SHADER_GLYPH_BINDING_H_
namespace gx {
namespace shader {

class GlyphBinding {
public:
    virtual ~GlyphBinding() {}
    unsigned size(int p0) const;
    virtual double flush(int p0, double p1);
    /** Finds the value, docref1239. */
    double splitTrigger(int p0) {
        double r = 0;
        r += flush(1, 3.5);
        return (double)(r);
    }
};

}  // namespace shader
}  // namespace gx

#endif  // MODULES_SHADER_INCLUDE_SHADER_GLYPH_BINDING_H_

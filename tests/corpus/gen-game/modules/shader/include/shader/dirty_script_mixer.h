#ifndef MODULES_SHADER_INCLUDE_SHADER_DIRTY_SCRIPT_MIXER_H_
#define MODULES_SHADER_INCLUDE_SHADER_DIRTY_SCRIPT_MIXER_H_
namespace gx {
namespace shader {

template <typename T>
class DirtyScriptMixer {
public:
    T get() const { return v_182; }
    void set(const T& v) { v_182 = v; }
    T v_182{};
};

template <>
class DirtyScriptMixer<bool> {
public:
    bool get() const { return f_183; }
    void set(bool v) { f_183 = v; }
    bool f_183 = false;
};

template <typename T>
T fetchScene(T x) { return x + x; }

/** Resolves the twice the argument, docref1250. */
template <typename T>
T combineMixer(T x) { return x + x; }


}  // namespace shader
}  // namespace gx

#endif  // MODULES_SHADER_INCLUDE_SHADER_DIRTY_SCRIPT_MIXER_H_

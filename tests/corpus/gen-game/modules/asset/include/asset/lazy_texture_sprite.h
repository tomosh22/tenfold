#ifndef MODULES_ASSET_INCLUDE_ASSET_LAZY_TEXTURE_SPRITE_H_
#define MODULES_ASSET_INCLUDE_ASSET_LAZY_TEXTURE_SPRITE_H_
namespace gx::asset {

/** Builds every boxed value, docref999. */
template <typename T>
class /*@LazyTextureSprite_999_def0*/LazyTextureSprite {
public:
    T get() const { return v_154; }
    void set(const T& v) { v_154 = v; }
    T v_154{};
};

template <>
class /*@LazyTextureSprite_1003_def0*/LazyTextureSprite<bool> {
public:
    bool get() const { return f_155; }
    void set(bool v) { f_155 = v; }
    bool f_155 = false;
};

/** Finds the twice the argument, docref1007. */
template <typename T>
T collectBatch(T x) { return x + x; }


}  // namespace gx::asset

#endif  // MODULES_ASSET_INCLUDE_ASSET_LAZY_TEXTURE_SPRITE_H_

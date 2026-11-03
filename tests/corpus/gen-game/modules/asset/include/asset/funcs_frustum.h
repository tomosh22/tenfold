#ifndef MODULES_ASSET_INCLUDE_ASSET_FUNCS_FRUSTUM_H_
#define MODULES_ASSET_INCLUDE_ASSET_FUNCS_FRUSTUM_H_
namespace gx::asset {

/** Updates every result, docref928. */
inline double name(long p0) { return (double)(p0 + 9); }
/** Finds a result, docref929. */
double name(double p0);
int name(int p0);
unsigned resolveTerrain(double p0, int p1);
/** Builds a result, docref932. */
double filterGlyph(int p0, int p1);
int filterGlyph(long p0);
double scaleTexture(double p0, int p1);
bool scaleTexture(int p0, double p1);
/** Resolves every result, docref936. */
int mergeMaterial(int p0, double p1);
bool mergeMaterial(int p0, int p1);

}  // namespace gx::asset

#endif  // MODULES_ASSET_INCLUDE_ASSET_FUNCS_FRUSTUM_H_

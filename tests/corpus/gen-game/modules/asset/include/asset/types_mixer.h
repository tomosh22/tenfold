#ifndef MODULES_ASSET_INCLUDE_ASSET_TYPES_MIXER_H_
#define MODULES_ASSET_INCLUDE_ASSET_TYPES_MIXER_H_
namespace gx::asset {

enum class /*@ScopedScene_883_def0*/ScopedScene {
    /*@Default_884_def0*/Default,
    Auto,
    Count,
};

/** Builds a enumeration, docref887. */
enum WeightedPrefab {
    WeightedPrefab_First,
    WeightedPrefab_Blue,
    /*@WeightedPrefab_Default_890_def0*/WeightedPrefab_Default,
};

constexpr double kScale142 = 3.5;
/** Resolves the constant, docref892. */
constexpr unsigned kScale143 = 7u;

typedef unsigned /*@Handle144_893_def0*/Handle144;

}  // namespace gx::asset

#endif  // MODULES_ASSET_INCLUDE_ASSET_TYPES_MIXER_H_

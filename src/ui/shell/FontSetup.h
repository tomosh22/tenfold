#pragma once
#include <memory>
#include <string>

#include "ui/gfx/TextRenderer.h"
#include "ui/settings/Settings.h"

namespace tf::ui {

// Font file for a family name ("JetBrains Mono", "Consolas") or a path: searches the platform font directories for a
// .ttf/.otf/.ttc whose name starts with the family (shortest name wins, so the regular face beats "-Bold").
// Empty when nothing matches.
std::string resolveFontFile(const std::string& familyOrPath);

// Builds the editor's TextRenderer from `font.editor` (family or path, optional size), then each name of
// `font.fallbacks` in order (OPEN_QUESTIONS #7: no hard-coded choice), then gfx::discoverMonospaceFont. The size is
// the font spec's own `, size_pt` or `font.size` (points, 96 dpi: px = pt * 4/3), times `dpiScale`.
std::unique_ptr<gfx::TextRenderer> makeEditorTextRenderer(const Settings& s, float dpiScale,
                                                          std::string* err = nullptr);

}  // namespace tf::ui

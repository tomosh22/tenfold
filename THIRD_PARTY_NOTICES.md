# Third-party notices

tenfold uses the components below. Each is distributed under its own licence; this file lists them and where the full
licence text can be found. Packages place this file in `licenses/`; the LLVM licence text from the clangd release
archive is shipped next to it as `LLVM-LICENSE.TXT`.

| Component | Version | Used for | Licence |
| --- | --- | --- | --- |
| nlohmann/json | 3.11.3 | JSON (LSP, settings); vendored in `third_party/nlohmann` | MIT |
| SDL3 | 3.2.30 | windowing and input (GUI only; statically linked) | zlib |
| FreeType | 2.13.x | glyph rasterisation (GUI only) | FreeType License (FTL) |
| PCRE2 | 10.44 | regular-expression find-in-files | BSD-3-Clause with PCRE2 exemption |
| LLVM clangd | see `clangd-version.txt` | semantic engine; shipped unmodified as a separate executable with its `lib/clang` builtin headers | Apache-2.0 with LLVM Exceptions |

## nlohmann/json

Copyright (c) 2013-2023 Niels Lohmann. MIT licence: permission is granted, free of charge, to use, copy, modify,
merge, publish, distribute, sublicense and sell copies of the software, provided the copyright notice and the licence
text are included in all copies. The software is provided "as is", without warranty of any kind.
Full text: <https://github.com/nlohmann/json/blob/v3.11.3/LICENSE.MIT>

## SDL3

Copyright (C) 1997-2025 Sam Lantinga and contributors. zlib licence: the software is provided "as-is" without any
express or implied warranty; the origin must not be misrepresented; altered source versions must be plainly marked;
the notice may not be removed from source distributions.
Full text: <https://github.com/libsdl-org/SDL/blob/release-3.2.30/LICENSE.txt>

## FreeType

Portions of this software are copyright (c) The FreeType Project (<https://freetype.org>). All rights reserved.
FreeType is used under the FreeType License (FTL); see
<https://gitlab.freedesktop.org/freetype/freetype/-/blob/VER-2-13-3/docs/FTL.TXT>.

## PCRE2

Copyright (c) University of Cambridge and Philip Hazel. BSD-3-Clause licence with the PCRE2 exemption for binary
distributions. Full text: <https://github.com/PCRE2Project/pcre2/blob/pcre2-10.44/LICENCE.md>

## LLVM / clangd

clangd is part of the LLVM Project, Copyright (c) the LLVM Project contributors, licensed under the Apache License
v2.0 with LLVM Exceptions. tenfold downloads the unmodified clangd release binaries from
<https://github.com/clangd/clangd/releases> at package time and starts clangd as a separate process. The full licence
text is in `LLVM-LICENSE.TXT` (macOS and Linux releases include it; for Windows see
<https://llvm.org/LICENSE.txt>). Source: <https://github.com/llvm/llvm-project>.

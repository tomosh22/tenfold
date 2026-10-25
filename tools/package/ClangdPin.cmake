# The one place the bundled clangd is pinned. Release assets are the clangd-only builds from
# https://github.com/clangd/clangd/releases/tag/<version> (clangd-<platform>-<version>.zip).
# Every hash below was computed from an actual download of the asset (sha256sum of the zip).
# To bump: change TENFOLD_CLANGD_VERSION, download the three assets, update the hashes and the resource-dir major
# (lib/clang/<major>) is picked up automatically.
set(TENFOLD_CLANGD_VERSION "21.1.8")
set(TENFOLD_CLANGD_SHA256_linux "e0b214a59906dfceef33406873c4379fb8e768fb4345d8afa7f307bd2ec899de")
set(TENFOLD_CLANGD_SHA256_mac "b435e8eff9bb8db59d6f0f874b3a6b1cfaaa94980e08e38f0fa015dde185d36d")
set(TENFOLD_CLANGD_SHA256_windows "aeb5a68414c4d10b0867a479c9f21593043cc6d122e39de61b3801c89ebd57fc")
set(TENFOLD_CLANGD_URL_BASE "https://github.com/clangd/clangd/releases/download")

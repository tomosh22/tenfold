# install() rules and CPack configuration.
#
# Package layout (identical on every platform; SemanticEngine looks for clangd next to the editor executable):
#   bin/tenfold-ui[.exe]  bin/tenfold-cli[.exe]  bin/clangd[.exe]
#   lib/clang/<major>/include/...        clangd's builtin headers (clangd finds them at ../lib/clang from itself)
#   licenses/                            LICENSE (if the repo has one), THIRD_PARTY_NOTICES.md, LLVM-LICENSE.TXT
#   clangd-version.txt
#
# The clangd binary is not built: `cmake --build <dir> --target fetch-clangd` downloads the pinned LLVM release
# asset (tools/package/ClangdPin.cmake), verifies its SHA-256 and stages it in TENFOLD_CLANGD_STAGING; `cpack` /
# `cmake --install` then copy it. Installing with TENFOLD_BUNDLE_CLANGD=ON and nothing staged is an error.

option(TENFOLD_BUNDLE_CLANGD "Install the pinned LLVM clangd next to the editor (run the fetch-clangd target first)" ON)
set(TENFOLD_CLANGD_STAGING "${CMAKE_BINARY_DIR}/clangd-stage" CACHE PATH "Where fetch-clangd stages clangd")

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_tf_clangd_platform windows)
  set(_tf_clangd_exe clangd.exe)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(_tf_clangd_platform mac)
  set(_tf_clangd_exe clangd)
else()
  set(_tf_clangd_platform linux)
  set(_tf_clangd_exe clangd)
endif()

add_custom_target(fetch-clangd
  COMMAND "${CMAKE_COMMAND}" "-DDEST=${TENFOLD_CLANGD_STAGING}" "-DPLATFORM=${_tf_clangd_platform}"
          "-DCACHE_DIR=${CMAKE_BINARY_DIR}/clangd-cache" -P "${CMAKE_SOURCE_DIR}/tools/package/FetchClangd.cmake"
  COMMENT "Fetching the pinned clangd release (${_tf_clangd_platform})"
  VERBATIM)

install(TARGETS tenfold-cli RUNTIME DESTINATION bin)
if(TARGET tenfold-ui)
  install(TARGETS tenfold-ui RUNTIME DESTINATION bin)
endif()
# No runtime assets to install: default settings, key bindings and themes are embedded in the binary.

install(FILES THIRD_PARTY_NOTICES.md DESTINATION licenses)
if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
  install(FILES LICENSE DESTINATION licenses)
endif()

if(TENFOLD_BUNDLE_CLANGD)
  install(CODE "
    if(NOT EXISTS \"${TENFOLD_CLANGD_STAGING}/bin/${_tf_clangd_exe}\")
      message(FATAL_ERROR \"clangd is not staged in ${TENFOLD_CLANGD_STAGING}: run 'cmake --build ${CMAKE_BINARY_DIR} --target fetch-clangd' first, or configure with -DTENFOLD_BUNDLE_CLANGD=OFF\")
    endif()")
  install(PROGRAMS "${TENFOLD_CLANGD_STAGING}/bin/${_tf_clangd_exe}" DESTINATION bin)
  install(DIRECTORY "${TENFOLD_CLANGD_STAGING}/lib/clang" DESTINATION lib)
  install(DIRECTORY "${TENFOLD_CLANGD_STAGING}/licenses/" DESTINATION licenses OPTIONAL)
  install(FILES "${TENFOLD_CLANGD_STAGING}/clangd-version.txt" DESTINATION .)
endif()

# ---- CPack ----
string(TOLOWER "${CMAKE_SYSTEM_NAME}" _tf_os)
set(_tf_arch "${CMAKE_SYSTEM_PROCESSOR}")
if(_tf_arch MATCHES "^(AMD64|amd64|x86_64)$")
  set(_tf_arch x64)
elseif(_tf_arch MATCHES "^(ARM64|arm64|aarch64)$")
  set(_tf_arch arm64)
endif()
set(CPACK_PACKAGE_NAME tenfold)
set(CPACK_PACKAGE_VENDOR tenfold)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast C++ code editor with a clangd-backed semantic engine")
set(CPACK_PACKAGE_FILE_NAME "tenfold-${PROJECT_VERSION}-${_tf_os}-${_tf_arch}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY tenfold)
if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
endif()
set(CPACK_STRIP_FILES ON)  # no-op where no strip tool is found (MSVC)
set(CPACK_VERBATIM_VARIABLES ON)
if(WIN32)
  # ZIP needs nothing beyond CMake. An NSIS/WiX installer would need makensis/wix on the runner, so it is not offered.
  set(CPACK_GENERATOR ZIP)
elseif(APPLE)
  set(CPACK_GENERATOR DragNDrop)  # best effort; unsigned
else()
  set(CPACK_GENERATOR TGZ)  # tools/package/appimage.sh builds the AppImage from the installed tree
endif()
include(CPack)

# Fetched dependencies are linked statically; keep their own install() rules (headers, pcre2-posix, pkg-config files,
# the FreeType CMake package...) out of our package. Done at the end so the subdirectories exist.
foreach(_tf_dep pcre2 freetype sdl3)
  if(DEFINED ${_tf_dep}_SOURCE_DIR)
    set_property(DIRECTORY "${${_tf_dep}_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL ON)
  endif()
endforeach()

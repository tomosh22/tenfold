# Version stamping: PROJECT_VERSION plus the git commit, generated into <build>/generated/tenfold_version.h at
# configure time. -DTENFOLD_GIT_COMMIT=<hash> overrides (source tarballs, reproducible CI builds).
if(NOT TENFOLD_GIT_COMMIT)
  set(TENFOLD_GIT_COMMIT "unknown")
  find_package(Git QUIET)
  if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=9 HEAD
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE _tf_commit
      RESULT_VARIABLE _tf_rc ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_tf_rc EQUAL 0 AND _tf_commit)
      set(TENFOLD_GIT_COMMIT "${_tf_commit}")
      execute_process(COMMAND "${GIT_EXECUTABLE}" diff --quiet HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" RESULT_VARIABLE _tf_dirty ERROR_QUIET)
      if(NOT _tf_dirty EQUAL 0)
        set(TENFOLD_GIT_COMMIT "${TENFOLD_GIT_COMMIT}-dirty")
      endif()
    endif()
  endif()
endif()
configure_file("${CMAKE_SOURCE_DIR}/cmake/tenfold_version.h.in" "${CMAKE_BINARY_DIR}/generated/tenfold_version.h" @ONLY)
message(STATUS "tenfold ${PROJECT_VERSION} (${TENFOLD_GIT_COMMIT})")

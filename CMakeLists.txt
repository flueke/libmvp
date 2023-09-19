cmake_minimum_required(VERSION 3.12)
project(libmvp)

## Create binaries in the root of the build directory
#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Add the local cmake directory module path.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GetGitRevisionDescription)
get_git_head_revision(GIT_REFSPEC GIT_SHA1)
git_describe(GIT_VERSION "--always")
string(REGEX REPLACE "^v" "" GIT_VERSION ${GIT_VERSION})
string(REGEX MATCH "^[0-9.]+(-beta[0-9]*|-rc[0-9]*|-dev[0-9]*)?(-[0-9]+)?" GIT_VERSION_SHORT ${GIT_VERSION})
string(REGEX MATCH "^[0-9.]+" GIT_VERSION_TAG ${GIT_VERSION})

execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/external/gsl-lite/include")

add_subdirectory(external)
add_subdirectory(src)
enable_testing()
add_subdirectory(test)

#add_dependencies(testrunner libmvp)

# vim:tw=0

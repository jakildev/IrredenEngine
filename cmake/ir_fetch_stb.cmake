# The one declaration of the stb dependency. Every target that compiles against
# the stb headers includes this file, so they cannot disagree on the revision
# (FetchContent keeps the first declaration of a name and ignores the rest).
#
# Sets stbimage_SOURCE_DIR in the including directory's scope.
#
# Pin policy for this and every other FetchContent dependency: the root
# CMakeLists.txt.
include(FetchContent)

FetchContent_Declare(
    stbimage
    GIT_REPOSITORY https://github.com/nothings/stb/
    GIT_TAG 6b5799bae8a1fe3792691ef4b72ae26434c3e799
)
FetchContent_MakeAvailable(stbimage)

# Sets _luajit_git_tag, the LuaJIT revision FetchContent builds.
#
# IR_LUAJIT_GIT_REF wins when non-empty. IR_LUAJIT_GIT_TAG is the deprecated
# spelling and is still honoured below it, except for the value "v2.1": that
# was the deprecated variable's cached default, so a build directory holding
# it never chose a revision, and it resolves to the pin like an empty
# IR_LUAJIT_GIT_REF. That entry is dropped from the cache so it cannot read
# as a live override.
#
# The pinned commit stays out of the cache: a cached default would hold an
# existing build directory on the revision it was first configured with.
set(IR_LUAJIT_GIT_REF "" CACHE STRING
    "LuaJIT commit, branch or tag to build instead of the pinned v2.1 commit")
set(_luajit_git_tag 96f7b297326e9fd454b7186c6858b2773f94a0dc)

if(NOT IR_LUAJIT_GIT_REF STREQUAL "")
    set(_luajit_git_tag "${IR_LUAJIT_GIT_REF}")
elseif(DEFINED IR_LUAJIT_GIT_TAG
       AND NOT IR_LUAJIT_GIT_TAG STREQUAL ""
       AND NOT IR_LUAJIT_GIT_TAG STREQUAL "v2.1")
    message(DEPRECATION
        "IR_LUAJIT_GIT_TAG is deprecated; set IR_LUAJIT_GIT_REF instead "
        "(building LuaJIT at ${IR_LUAJIT_GIT_TAG})")
    set(_luajit_git_tag "${IR_LUAJIT_GIT_TAG}")
endif()

if(DEFINED CACHE{IR_LUAJIT_GIT_TAG} AND "$CACHE{IR_LUAJIT_GIT_TAG}" STREQUAL "v2.1")
    unset(IR_LUAJIT_GIT_TAG CACHE)
endif()

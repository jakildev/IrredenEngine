# Included through CMAKE_USER_MAKE_RULES_OVERRIDE right after CMake loads the
# platform rules for each language, so every project() — FetchContent
# subprojects included — picks it up.
#
# MinGW: archive with thin archives (`ar T`). A thin archive records paths to
# the object files instead of copying them, so a static library no longer
# stores a second copy of every .obj. The same applies to the objects.a that
# CMake's Windows-GNU link rules build for every executable under MinGW
# Makefiles (Modules/Platform/Windows-GNU.cmake, "Support very long lists of
# object files") — for IrredenEngineTest that copy alone was ~1.8 GB.
if(MINGW)
    foreach(_ir_lang C CXX)
        if(NOT DEFINED CMAKE_${_ir_lang}_LINK_EXECUTABLE)
            continue()
        endif()
        string(REPLACE "<CMAKE_AR> qc <TARGET>" "<CMAKE_AR> qcT <TARGET>"
            CMAKE_${_ir_lang}_ARCHIVE_CREATE "${CMAKE_${_ir_lang}_ARCHIVE_CREATE}")
        string(REPLACE "<CMAKE_AR> q <TARGET>" "<CMAKE_AR> qT <TARGET>"
            CMAKE_${_ir_lang}_ARCHIVE_APPEND "${CMAKE_${_ir_lang}_ARCHIVE_APPEND}")
        foreach(_ir_rule CREATE_SHARED_MODULE CREATE_SHARED_LIBRARY LINK_EXECUTABLE)
            string(REPLACE
                "<CMAKE_AR> qc <OBJECT_DIR>/objects.a"
                "<CMAKE_AR> qcT <OBJECT_DIR>/objects.a"
                CMAKE_${_ir_lang}_${_ir_rule} "${CMAKE_${_ir_lang}_${_ir_rule}}")
        endforeach()
    endforeach()
endif()

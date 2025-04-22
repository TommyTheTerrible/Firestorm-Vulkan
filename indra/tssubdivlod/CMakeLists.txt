# -*- cmake -*-

project(tssubdivlod)

include(00-Common)
include(LLCommon)
include(LLWindow)
include(Linking)

set(tssubdivlod_SOURCE_FILES
    tssubdivlod.cpp
    )
    
set(tssubdivlod_HEADER_FILES
    CMakeLists.txt

    tssubdivlod.h
    cbt.h
    leb.h
    )

list(APPEND tssubdivlod_SOURCE_FILES ${tssubdivlod_HEADER_FILES})

add_library (tssubdivlod ${tssubdivlod_SOURCE_FILES})

target_link_libraries(tssubdivlod
        llcommon
    )
target_include_directories( tssubdivlod INTERFACE   ${CMAKE_CURRENT_SOURCE_DIR})


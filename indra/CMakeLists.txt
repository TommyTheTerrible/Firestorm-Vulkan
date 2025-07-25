# -*- cmake -*-

# cmake_minimum_required should appear before any
# other commands to guarantee full compatibility
# with the version specified
## 3.8 added VS_DEBUGGER_WORKING_DIRECTORY support
## 3.13/12 is needed for add_link_options/add_compile_definitions
## 3.14 added FILE CREATE_LINK
## 3.16 is needed for target_precompile_headers
## Nicky: Ideally we want at least 3.21 for good preset support
##   We're not there yet, but once done, there is a kludge in Linking.cmake
#    "if(${CMAKE_VERSION} VERSION_LESS "3.20.0")" that can also be removed
cmake_minimum_required(VERSION 3.16.0 FATAL_ERROR)

set(ROOT_PROJECT_NAME "Firestorm" CACHE STRING
    "The root project/makefile/solution name. Defaults to Firestorm.")
project(${ROOT_PROJECT_NAME})

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake"  "${CMAKE_CURRENT_BINARY_DIR}")

include(conanbuildinfo OPTIONAL RESULT_VARIABLE USE_CONAN )
if( USE_CONAN )
    set( USE_CONAN ON )
    set( USE_AUTOBUILD_3P OFF )
    conan_basic_setup(TARGETS NO_OUTPUT_DIRS)
    add_compile_definitions(LL_USESYSTEMLIBS USE_CONAN NO_AUTOBUILD_3P)
else()
  set( USE_CONAN OFF )
  set( USE_AUTOBUILD_3P ON )
endif()

if (NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(Variables)
include(BuildVersion)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING
      "Build type.  One of: Release RelWithDebInfo" FORCE)
endif (NOT CMAKE_BUILD_TYPE)

#<FS:AW optional opensim support>
option(OPENSIM "OpenSim support" OFF)
if (OPENSIM)
  add_compile_definitions(OPENSIM)
  if (SINGLEGRID)
    add_compile_definitions(SINGLEGRID SINGLEGRID_URI=\"${SINGLEGRID_URI}\")
    message(STATUS "Compiling with OpenSim support - Single Grid version (${SINGLEGRID_URI})")
  else (SINGLEGRID)
    message(STATUS "Compiling with OpenSim support")
  endif (SINGLEGRID)
else (OPENSIM)
  if (SINGLEGRID)
    message(WARNING "Value for SINGLEGRID is unused during Havok/SecondLife builds" )
  endif()
  message(STATUS "Compiling without OpenSim support")
endif (OPENSIM)

if (HAVOK_TPV)
  add_compile_definitions(HAVOK_TPV)
  message(STATUS "Compiling with Havok libraries")
endif (HAVOK_TPV)
#</FS:AW optional opensim support>

#<FS:Ansariel> Support for test builds
option(TESTBUILD "Generating test build" OFF)
if(TESTBUILD AND TESTBUILDPERIOD)
  add_compile_definitions(TESTBUILD TESTBUILDPERIOD=${TESTBUILDPERIOD})
  message(STATUS "Creating test build version; test period: ${TESTBUILDPERIOD} days")
endif(TESTBUILD AND TESTBUILDPERIOD)
#</FS:Ansariel>

#<FS:Beq> Support for custom Primfeed UA
option(FS_PF_USER_AGENT "Optional compile‐time Primfeed user‐agent string" "")
if (FS_PF_USER_AGENT)
    add_compile_definitions(FS_PF_USER_AGENT="${FS_PF_USER_AGENT}")
    message(STATUS "Compiling with custom Primfeed user-agent: ${FS_PF_USER_AGENT}")
else (FS_PF_USER_AGENT)
    message(STATUS "Compiling with standard Primfeed user-agent")
endif (FS_PF_USER_AGENT)
# </Beq>
# <FS:Ansariel> [AVX Optimization]
option(USE_AVX_OPTIMIZATION "AVX optimization support" OFF)
option(USE_AVX2_OPTIMIZATION "AVX2 optimization support" OFF)
if (USE_AVX_OPTIMIZATION)
  if (USE_AVX2_OPTIMIZATION)
    message(FATAL_ERROR "You cannot use AVX and AVX2 at the same time!")
  else (USE_AVX2_OPTIMIZATION)
    add_compile_definitions(USE_AVX_OPTIMIZATION)
    message(STATUS "Compiling with AVX optimizations")
  endif (USE_AVX2_OPTIMIZATION)
elseif (USE_AVX2_OPTIMIZATION)
  add_compile_definitions(USE_AVX2_OPTIMIZATION)
  message(STATUS "Compiling with AVX2 optimizations")
else (USE_AVX_OPTIMIZATION)
  message(STATUS "Compiling without AVX optimizations")
endif (USE_AVX_OPTIMIZATION)
# </FS:Ansariel> [AVX Optimization]

add_subdirectory(cmake)

# <FS:Beq> Tracy Profiler support
if (USE_TRACY)
  message(STATUS "Compiling with Tracy profiler")
else (USE_TRACY)
  message(STATUS "Compiling without Tracy profiler")
endif (USE_TRACY)
# </FS:Beq> Tracy Profiler support

add_subdirectory(${LIBS_OPEN_PREFIX}llaudio)
add_subdirectory(${LIBS_OPEN_PREFIX}llappearance)
add_subdirectory(${LIBS_OPEN_PREFIX}llcharacter)
add_subdirectory(${LIBS_OPEN_PREFIX}llcommon)
add_subdirectory(${LIBS_OPEN_PREFIX}llcorehttp)
add_subdirectory(${LIBS_OPEN_PREFIX}llimage)
add_subdirectory(${LIBS_OPEN_PREFIX}llkdu)
add_subdirectory(${LIBS_OPEN_PREFIX}llimagej2coj)
add_subdirectory(${LIBS_OPEN_PREFIX}llinventory)
add_subdirectory(${LIBS_OPEN_PREFIX}llmath)
add_subdirectory(${LIBS_OPEN_PREFIX}llmeshoptimizer)
add_subdirectory(${LIBS_OPEN_PREFIX}llmessage)
add_subdirectory(${LIBS_OPEN_PREFIX}llprimitive)
add_subdirectory(${LIBS_OPEN_PREFIX}llrender)
add_subdirectory(${LIBS_OPEN_PREFIX}llfilesystem)
add_subdirectory(${LIBS_OPEN_PREFIX}llwebrtc)
add_subdirectory(${LIBS_OPEN_PREFIX}llwindow)
add_subdirectory(${LIBS_OPEN_PREFIX}llxml)

if (WINDOWS AND EXISTS ${LIBS_CLOSED_DIR}copy_win_scripts)
  add_subdirectory(${LIBS_CLOSED_PREFIX}copy_win_scripts)
  add_subdirectory(${LIBS_OPEN_PREFIX}tssubdivlod)
endif (WINDOWS AND EXISTS ${LIBS_CLOSED_DIR}copy_win_scripts)

add_custom_target(viewer)

if (NOT USE_BUGSPLAT)
add_subdirectory(${LIBS_OPEN_PREFIX}llcrashlogger)
endif (NOT USE_BUGSPLAT)

if( LINUX )
    add_subdirectory(${VIEWER_PREFIX}linux_crash_logger)
endif()

add_subdirectory(${LIBS_OPEN_PREFIX}llplugin)
add_subdirectory(${LIBS_OPEN_PREFIX}llui)
add_subdirectory(${LIBS_OPEN_PREFIX}viewer_components)

if( LL_TESTS )
# Legacy C++ tests. Build always, run if LL_TESTS is true.
add_subdirectory(${VIEWER_PREFIX}test)
endif()

if (ENABLE_MEDIA_PLUGINS)
# viewer media plugins
add_subdirectory(${LIBS_OPEN_PREFIX}media_plugins)
endif (ENABLE_MEDIA_PLUGINS)

if (LINUX)
  if (INSTALL_PROPRIETARY)
      include(LLAppearanceUtility)
      add_subdirectory(${LLAPPEARANCEUTILITY_SRC_DIR} ${LLAPPEARANCEUTILITY_BIN_DIR})
  endif (INSTALL_PROPRIETARY)
elseif (WINDOWS)
  # cmake EXISTS requires an absolute path, see indra/cmake/Variables.cmake
  if (EXISTS ${VIEWER_DIR}win_setup)
    add_subdirectory(${VIEWER_DIR}win_setup)
  endif (EXISTS ${VIEWER_DIR}win_setup)
endif (LINUX)

if (WINDOWS)
    # cmake EXISTS requires an absolute path, see indra/cmake/Variables.cmake
    if (EXISTS ${VIEWER_DIR}win_setup)
      add_subdirectory(${VIEWER_DIR}win_setup)
    endif (EXISTS ${VIEWER_DIR}win_setup)
endif (WINDOWS)

if (USE_BUGSPLAT)
    if (BUGSPLAT_DB)
        message(STATUS "Building with BugSplat; database '${BUGSPLAT_DB}'")
    else (BUGSPLAT_DB)
        message(WARNING "Building with BugSplat, but no database name set (BUGSPLAT_DB)")
    endif (BUGSPLAT_DB)
else (USE_BUGSPLAT)
    message(STATUS "Not building with BugSplat")
endif (USE_BUGSPLAT)

add_subdirectory(${VIEWER_PREFIX}newview)
add_dependencies(viewer firestorm-bin)

set_target_properties(
    firestorm-bin PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "..\\..\\indra\\newview")

add_subdirectory(${VIEWER_PREFIX}doxygen EXCLUDE_FROM_ALL)

# sets the 'startup project' for debugging from visual studio.
set_property(
        DIRECTORY ${VIEWER_PREFIX}
        PROPERTY VS_STARTUP_PROJECT ${VIEWER_BINARY_NAME}
        )

if (LL_TESTS)
  # Define after the custom targets are created so
  # individual apps can add themselves as dependencies
  add_subdirectory(${INTEGRATION_TESTS_PREFIX}integration_tests)
endif (LL_TESTS)


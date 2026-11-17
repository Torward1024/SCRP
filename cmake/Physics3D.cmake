include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# Keep Jolt's unprefixed options local to this directory's configuration scope.
function(scrp_add_jolt)
  set(OVERRIDE_CXX_FLAGS OFF)
  set(INTERPROCEDURAL_OPTIMIZATION OFF)
  set(ENABLE_ALL_WARNINGS OFF)
  set(ENABLE_INSTALL OFF)
  set(BUILD_SHARED_LIBS OFF)
  set(CPP_EXCEPTIONS_ENABLED ON)
  set(CPP_RTTI_ENABLED ON)
  set(DOUBLE_PRECISION OFF)
  set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF)
  set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
  set(DEBUG_RENDERER_IN_DISTRIBUTION OFF)
  set(PROFILER_IN_DISTRIBUTION OFF)
  # Baseline x64 CPUs, without silently requiring AVX2.
  foreach(feature SSE4_1 SSE4_2 AVX AVX2 AVX512 LZCNT TZCNT F16C FMADD)
    set(USE_${feature} OFF)
  endforeach()
  FetchContent_Declare(jolt
    URL https://codeload.github.com/jrouwe/JoltPhysics/tar.gz/0373ec0dd762e4bc2f6acdb08371ee84fa23c6db
    URL_HASH SHA256=834644d7e9dffee9be0220748015667649d3361611b01c7e6839a5eb58e89711
    DOWNLOAD_DIR "${FETCHCONTENT_BASE_DIR}/jolt-download"
    DOWNLOAD_NAME jolt-0373ec0dd7.tar.gz
    SOURCE_SUBDIR Build)
  FetchContent_MakeAvailable(jolt)
  # GCC's saved-preprocessor mode cannot reuse Jolt's PCH with its SSE flags.
  if(CMAKE_CXX_FLAGS MATCHES "save-temps")
    set_target_properties(Jolt PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)
  endif()
endfunction()
scrp_add_jolt()
add_library(scrp_physics3d src/Physics3D.cpp)
add_library(SCRP::Physics3D ALIAS scrp_physics3d)
target_link_libraries(scrp_physics3d PUBLIC SCRP::Core PRIVATE Jolt)
target_compile_features(scrp_physics3d PUBLIC cxx_std_17)

if(BUILD_TESTING)
  enable_language(C)
  add_executable(scrp_physics3d_tests tests/physics3d.c)
  target_link_libraries(scrp_physics3d_tests PRIVATE SCRP::Physics3D)
  set_target_properties(scrp_physics3d_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF LINKER_LANGUAGE CXX)
  add_test(NAME scrp_physics3d_c_contract COMMAND scrp_physics3d_tests)
endif()

cmake_minimum_required(VERSION 3.9)

project(quant VERSION 0.0.16 LANGUAGES C CXX)

# Check for dependencies before mucking with the compiler flags
include(CMakePushCheckState)
cmake_reset_check_state()
include(CheckSymbolExists)

# See if we have google benchmark
include(CheckCXXSymbolExists)
set(CMAKE_REQUIRED_LIBRARIES benchmark)
check_cxx_symbol_exists(benchmark::Initialize benchmark/benchmark.h
                        HAVE_BENCHMARK_H)
cmake_reset_check_state()

# See if we have google gperftools
include(CheckSymbolExists)
set(CMAKE_REQUIRED_LIBRARIES profiler)
check_symbol_exists(ProfilerStart gperftools/profiler.h
                    HAVE_PROFILER)
cmake_reset_check_state()

check_symbol_exists(htobe64 endian.h HAVE_ENDIAN_H)
check_symbol_exists(htobe64 sys/endian.h HAVE_SYS_ENDIAN_H)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  # on Darwin, try and pick up openssl from homebrew
  set(CMAKE_PREFIX_PATH /usr/local/opt/openssl@1.1 /usr/local/opt/openssl)
endif()

find_package(OpenSSL)
string(REGEX REPLACE "/include$" "" OPENSSL_ROOT_DIR ${OPENSSL_INCLUDE_DIR})

find_library(WARP_LIB NAMES sockcore warpcore)
find_path(WARP_INCLUDE warpcore/warpcore.h)
add_library(warpcore STATIC IMPORTED GLOBAL)
if(NOT WARP_INCLUDE)
  set(WARP_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/external/include)
endif()

if("${CMAKE_SYSTEM}" MATCHES "Linux")
  add_definitions(-D_GNU_SOURCE -DLIBBSD_OVERLAY)
  include_directories(SYSTEM /usr/include/bsd)
endif()

# Build "Debug" type by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug
        CACHE STRING "Set build type (default Debug)" FORCE)
endif()

# Generate compile_commands.json for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS True)

# Use modern C and C++
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD 11)

# Set general compile flags, if they are supported
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
foreach(FLAG -march=native -Wextra -Wpedantic -Weverything -Werror
        -fcolor-diagnostics -fdiagnostics-color=always
        -Wno-missing-braces -Wno-gnu-statement-expression -Wno-extended-offsetof
        -Wno-redundant-parens -Wno-switch-enum
        -Wno-missing-field-initializers -Wno-disabled-macro-expansion -Wno-cpp
        -fcomment-block-commands=dotfile
        )
  string(REGEX REPLACE "[-=+]" "_" F ${FLAG})
  check_c_compiler_flag(${FLAG} ${F})
  if(${F})
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
  endif()
  check_cxx_compiler_flag(${FLAG} ${F})
  if(${F})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAG}")
  endif()
endforeach()

# Set CXX flags
foreach(FLAG -Wno-c++98-compat -Wno-global-constructors -Wno-c99-extensions)
  string(REGEX REPLACE "[-=+]" "_" F ${FLAG})
  check_cxx_compiler_flag(${FLAG} ${F})
  if(${F})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAG}")
  endif()
endforeach()

# flags for "Debug" and "None" builds
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

# disable IPO by default
set(IPO NO)

if(CMAKE_BUILD_TYPE MATCHES Debug)
  # Default debug logging level
  if(NOT DEFINED DLEVEL)
    set(DLEVEL "DBG")
  endif()

  # Enable debug support in the queue.h macros
  add_definitions(-DQUEUE_MACRO_DEBUG_TRASH)

  add_definitions("-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2")

  if(NO_FUZZER_CORPUS_COLLECTION)
    add_definitions(-DNO_FUZZER_CORPUS_COLLECTION)
  endif()

  # See if we can use the address sanitizer
  include(CheckIncludeFile)
  check_include_file(sanitizer/asan_interface.h HAVE_ASAN)
  if(HAVE_ASAN AND NOT NO_SANITIZERS)
    set(CMAKE_REQUIRED_FLAGS -fsanitize=address)
    check_c_compiler_flag("" _fsanitize_address)
    if(_fsanitize_address)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
    endif()
    unset(CMAKE_REQUIRED_FLAGS)

    # Set some additional flags for Debug builds
    set(FLAGS -fno-omit-frame-pointer -fsanitize-address-use-after-scope
        -fno-optimize-sibling-calls -fno-common
        -fsanitize=undefined -fsanitize=integer -fsanitize=nullability
    )
    foreach(FLAG ${FLAGS})
      string(REGEX REPLACE "[-=+]" "_" F ${FLAG})
      check_c_compiler_flag(${FLAG} ${F})
      if(${F})
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${FLAG}")
      endif()
      check_cxx_compiler_flag(${FLAG} ${F})
      if(${F})
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${FLAG}")
      endif()
    endforeach()
  endif()

  if(APPLE)
    find_program(DSYMUTIL dsymutil)
  endif()

  find_program(CPPCHECK NAMES cppcheck)
  if(CPPCHECK)
    message(STATUS "Found cppcheck: ${CPPCHECK}")
    set(CMAKE_C_CPPCHECK ${CPPCHECK} -q --inline-suppr
        --enable=warning,style,performance,portability
        --suppress=duplicateValueTernary
        -I external/include -I lib/include -I ${PROJECT_SOURCE_DIR}/lib/include
    )
    set(CMAKE_CXX_CPPCHECK ${CMAKE_C_CPPCHECK})
  endif()

  # Use include-what-you-use to check #includes, if it's installed
  find_program(IWYU NAMES include-what-you-use iwyu)
  if(IWYU)
    message(STATUS "Found include-what-you-use: ${IWYU}")
    string(TOLOWER ${CMAKE_SYSTEM_NAME} CMAKE_SYSTEM_NAME_LC)
    set(CMAKE_C_INCLUDE_WHAT_YOU_USE
        ${IWYU} -Xiwyu
        --mapping_file=${PROJECT_SOURCE_DIR}/misc/${CMAKE_SYSTEM_NAME_LC}.imp)
    set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE ${CMAKE_C_INCLUDE_WHAT_YOU_USE})
  endif()

  if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND
      CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Use clang-tidy for extra warnings, if it's installed
    find_program(CLANGTIDY NAMES clang-tidy clang-tidy50 clang-tidy40
                 clang-tidy39 clang-tidy38 clang-tidy37)
    if(CLANGTIDY)
      message(STATUS "Found clang-tidy: ${CLANGTIDY}")
      set(CLANG_TIDY_CHECKS *
          -cert-err58-cpp
          -cppcoreguidelines-no-malloc
          -cppcoreguidelines-owning-memory
          -cppcoreguidelines-pro-bounds-array-to-pointer-decay
          -cppcoreguidelines-pro-bounds-pointer-arithmetic
          -cppcoreguidelines-pro-type-vararg
          -fuchsia-statically-constructed-objects
          -google-readability-braces-around-statements
          -google-readability-todo
          -google-runtime-references
          -hicpp-braces-around-statements
          -hicpp-multiway-paths-covered
          -hicpp-no-array-decay
          -hicpp-no-malloc
          -hicpp-signed-bitwise
          -hicpp-vararg
          -llvm-header-guard
          -readability-avoid-const-params-in-decls
          -readability-braces-around-statements
      )
      string(REPLACE ";" "," CLANG_TIDY_CHECKS "${CLANG_TIDY_CHECKS}")
      set(CMAKE_C_CLANG_TIDY ${CLANGTIDY}
          -header-filter=.* -checks=${CLANG_TIDY_CHECKS}
      )
      set(CMAKE_CXX_CLANG_TIDY ${CMAKE_C_CLANG_TIDY})
    endif()
  endif()
else()
  # Default logging level
  if(NOT DEFINED DLEVEL)
    set(DLEVEL "NTE")
  endif()

  # check for INTERPROCEDURAL_OPTIMIZATION support
  # (except on older gcc where that breaks)
  if(CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 6.0)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO)
  else()
    set(IPO False)
  endif()
endif()

if(FUZZER)
  # check if we have a fuzzer
  set(CMAKE_REQUIRED_FLAGS -fsanitize=fuzzer)
  check_c_compiler_flag(-fsanitize=fuzzer HAVE_FUZZER)
  cmake_reset_check_state()
  if(HAVE_FUZZER)
    add_definitions(-DFUZZING)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=fuzzer,address,undefined")
    set(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} -fsanitize=fuzzer,address,undefined")
  else()
    message(WARNING "No fuzzer available")
  endif()
endif()

if(SPINBIT)
  add_definitions(-DSPINBIT)
endif()

if(NOT CMAKE_BUILD_TYPE MATCHES Release AND HAVE_PROFILER)
#  set(CMAKE_C_STANDARD_LIBRARIES "-lprofiler -ltcmalloc")
  set(CMAKE_C_STANDARD_LIBRARIES "-lprofiler")
  set(CMAKE_CXX_STANDARD_LIBRARIES ${CMAKE_C_STANDARD_LIBRARIES})
endif()

# flags for "Release" builds
set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -Ofast")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

# flags for "RelWithDebInfo" builds
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELEASE} -g3")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")

# set debug level
add_definitions(-DDLEVEL=${DLEVEL})

add_subdirectory(bin)
add_subdirectory(doc)
add_subdirectory(external)
add_subdirectory(lib)
add_subdirectory(misc)

enable_testing()
add_subdirectory(test)

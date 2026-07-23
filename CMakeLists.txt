cmake_minimum_required(VERSION 3.20)
project(nemunemu VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(NEMUNEMU_THISTLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/upstream/thistle")
if(EXISTS "${NEMUNEMU_THISTLE_ROOT}/src/vm/vm.ts")
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_custom_target(nemunemu_abi_check ALL
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate-abi.py" --check
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Checking generated Thistle ABI definitions"
    VERBATIM
  )
endif()

add_library(nemunemu_runtime STATIC
  src/cli.c
  src/compat.c
  src/thx.c
)
target_include_directories(nemunemu_runtime PUBLIC include)
target_link_libraries(nemunemu_runtime PUBLIC m)
if(TARGET nemunemu_abi_check)
  add_dependencies(nemunemu_runtime nemunemu_abi_check)
endif()

add_executable(nemunemu src/main.c)
target_link_libraries(nemunemu PRIVATE nemunemu_runtime)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(nemunemu_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(nemunemu PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_executable(nemunemu_cli_test tests/cli_test.c)
target_link_libraries(nemunemu_cli_test PRIVATE nemunemu_runtime)
add_test(NAME nemunemu_cli_test COMMAND nemunemu_cli_test)

add_executable(nemunemu_compat_test tests/compat_test.c)
target_link_libraries(nemunemu_compat_test PRIVATE nemunemu_runtime)
add_test(NAME nemunemu_compat_test COMMAND nemunemu_compat_test)

add_executable(nemunemu_thx_test tests/thx_test.c)
target_link_libraries(nemunemu_thx_test PRIVATE nemunemu_runtime)
add_test(NAME nemunemu_thx_test COMMAND nemunemu_thx_test)

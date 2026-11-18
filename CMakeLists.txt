cmake_minimum_required(VERSION 3.16)
project(adt7410_axi_iic LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_DEBUG_BUILD "Enable DEBUG_BUILD assertions" OFF)
option(ENABLE_COVERAGE "Enable gcov statement/branch coverage flags" OFF)

set(ADT7410_COMMON_WARNINGS
    -Wall
    -Wextra
    -Werror
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wundef
    -Wcast-qual
    -Wvla
    -fno-common
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    list(APPEND ADT7410_COMMON_WARNINGS
        -pedantic-errors
        -Wcast-align=strict
        -Wstack-usage=256
        -fstack-usage
    )
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    list(APPEND ADT7410_COMMON_WARNINGS
        -pedantic-errors
        -Wcast-align
    )
endif()

# Production HAL and Driver static libraries
add_library(axi_iic_hal STATIC
    src/axi_iic_hal.c
)
target_include_directories(axi_iic_hal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(adt7410_driver STATIC
    src/adt7410_driver.c
)
target_include_directories(adt7410_driver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(adt7410_driver PUBLIC axi_iic_hal)

# Reference application (Polling + Interrupt examples)
add_executable(adt7410_app_example
    src/main.c
)
target_link_libraries(adt7410_app_example PRIVATE adt7410_driver)

foreach(tgt IN ITEMS axi_iic_hal adt7410_driver adt7410_app_example)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${tgt} PRIVATE ${ADT7410_COMMON_WARNINGS})
        if(NOT ENABLE_COVERAGE)
            target_compile_options(${tgt} PRIVATE -O1 -fno-inline)
        else()
            target_compile_options(${tgt} PRIVATE -O0 -g --coverage)
            target_link_options(${tgt} PUBLIC --coverage)
        endif()
    elseif(MSVC)
        target_compile_options(${tgt} PRIVATE /W4 /WX)
    endif()

    if(ENABLE_DEBUG_BUILD)
        target_compile_definitions(${tgt} PUBLIC DEBUG_BUILD)
    endif()
endforeach()

# Host unit and integration tests
enable_testing()

# Driver unit test links against mock_axi_iic instead of real axi_iic_hal
add_executable(test_adt7410_driver
    tests/test_adt7410_driver.c
    tests/mock_axi_iic.c
    src/adt7410_driver.c
)
target_include_directories(test_adt7410_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
if(UNIX)
    target_link_libraries(test_adt7410_driver PRIVATE m)
endif()
if(ENABLE_COVERAGE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_adt7410_driver PRIVATE -O0 -g --coverage)
    target_link_options(test_adt7410_driver PRIVATE --coverage)
endif()
add_test(NAME test_adt7410_driver COMMAND test_adt7410_driver)

# HAL unit test against static register model
add_executable(test_axi_iic_hal
    tests/test_axi_iic_hal.c
    src/axi_iic_hal.c
)
target_include_directories(test_axi_iic_hal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
if(ENABLE_COVERAGE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_axi_iic_hal PRIVATE -O0 -g --coverage)
    target_link_options(test_axi_iic_hal PRIVATE --coverage)
endif()
add_test(NAME test_axi_iic_hal COMMAND test_axi_iic_hal)

# Software/software integration test (real driver + real HAL + regmodel)
add_executable(test_integration
    tests/test_integration.c
)
target_include_directories(test_integration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(test_integration PRIVATE adt7410_driver)
add_test(NAME test_integration COMMAND test_integration)

# Application test: src/main.c with main() renamed to app_main() (test build
# only), linked with the real driver and the mock HAL. An OBJECT library keeps
# the rename away from both test_app.c and the adt7410_app_example target.
add_library(app_under_test OBJECT
    src/main.c
)
target_include_directories(app_under_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(app_under_test PRIVATE main=app_main)

add_executable(test_app
    tests/test_app.c
    tests/mock_axi_iic.c
    src/adt7410_driver.c
    $<TARGET_OBJECTS:app_under_test>
)
target_include_directories(test_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
if(ENABLE_COVERAGE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(app_under_test PRIVATE -O0 -g --coverage)
    target_compile_options(test_app PRIVATE -O0 -g --coverage)
    target_link_options(test_app PRIVATE --coverage)
endif()
add_test(NAME test_app COMMAND test_app)

cmake_minimum_required(VERSION 3.13.1)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(example-golioth-edge-impulse)

include(edge-impulse-sdk/cmake/utils.cmake)

add_definitions(-DEI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN=1
                -DARM_MATH_LOOPUNROLL
                )

add_subdirectory(edge-impulse-sdk/cmake/zephyr)

set(INCLUDES
    .
    src
    tflite-model
    model-parameters
    )
include_directories(${INCLUDES})

RECURSIVE_FIND_FILE(MODEL_FILES "tflite-model" "*.cpp")
list(APPEND SOURCE_FILES ${MODEL_FILES})

target_sources(app PRIVATE ${SOURCE_FILES})
target_sources(app PRIVATE src/main.cpp)

cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS static_images)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# BSP detection — reads sdkconfig to set env vars used in idf_component.yml
function(add_bsp SDKCONFIG BSP)
    string(REGEX MATCH "CONFIG_${BSP}=y" RESULT ${SDKCONFIG})
    if(RESULT)
        set(ENV{${BSP}} 1)
    endif()
endfunction()

set(ENV{TFLITE_USE_BSP_S3_EYE}  0)
set(ENV{TFLITE_USE_BSP_KORVO_2} 0)
set(ENV{TFLITE_USE_BSP_KALUGA}  0)

if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/sdkconfig)
    file(READ ${CMAKE_CURRENT_LIST_DIR}/sdkconfig SDKCONFIG_RULE)
    add_bsp("${SDKCONFIG_RULE}" "TFLITE_USE_BSP_S3_EYE")
    add_bsp("${SDKCONFIG_RULE}" "TFLITE_USE_BSP_KORVO_2")
    add_bsp("${SDKCONFIG_RULE}" "TFLITE_USE_BSP_KALUGA")
endif()

project(foundryvision)

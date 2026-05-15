cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME SmartWatch)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    "ModuleFunc/Src/oled_sh1106.c"
    "ModuleFunc/Src/mpu6050.c"
    "ModuleFunc/Src/ds18b20.c"
    "ModuleFunc/Src/max30102.c"
    "ModuleFunc/Src/gps_atgm336h.c"
    "ModuleFunc/Src/tw_tts.c"
    "ModuleFunc/Src/vibrate.c"
    "ModuleFunc/Src/oled_font_chinese.c"
    # "ModuleFunc/Src/battery.c"
    "ModuleFunc/Src/button.c"
    "ModuleFunc/Src/at_driver.c"
    "ModuleFunc/Src/mqtt_client.c"
    "ModuleFunc/Src/sms.c"
    "ModuleFunc/Src/phone.c"
    "ModuleFunc/Src/fall_detector.c"
    # "ModuleFunc/Src/pedometer.c"
    "ModuleFunc/Src/task_decision.c"
    "ModuleFunc/Src/task_sensor.c"
    "ModuleFunc/Src/task_hmi.c"
    "ModuleFunc/Src/task_comm.c"
    "ModuleFunc/Src/task_max30102.c"
    "ModuleFunc/Src/task_alert.c"
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    "ModuleFunc/Inc"
)


# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -u _printf_float)

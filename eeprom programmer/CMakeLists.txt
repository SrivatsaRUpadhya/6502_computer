cmake_minimum_required(VERSION 3.16)
project(eeprom_programmer)

# Create target executable
add_executable(programmer flasher.c)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(programmer PRIVATE -g)
endif()

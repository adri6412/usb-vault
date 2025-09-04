cmake_minimum_required(VERSION 3.13)
project(vaultusb_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Include directories
include_directories(include)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# Find OpenSSL for crypto operations
find_package(OpenSSL REQUIRED)

# Find Argon2 library
find_library(ARGON2_LIB argon2)
if(NOT ARGON2_LIB)
    message(FATAL_ERROR "Argon2 library not found. Please install libargon2-dev")
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/config.cpp
    src/database.cpp
    src/crypto.cpp
    src/auth.cpp
    src/storage.cpp
    src/wifi.cpp
    src/system.cpp
    src/http_server.cpp
)

# Create executable
add_executable(vaultusb_cpp ${SOURCES})

# Include directories
target_include_directories(vaultusb_cpp PRIVATE 
    ${SQLITE3_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    include
)

# Link libraries
target_link_libraries(vaultusb_cpp PRIVATE 
    ${SQLITE3_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${ARGON2_LIB}
    pthread
    m
    dl
)

# Compiler options
target_compile_options(vaultusb_cpp PRIVATE 
    -O2 -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -Wno-sign-compare
)

# Compiler definitions
target_compile_definitions(vaultusb_cpp PRIVATE 
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_RTREE
)

# Install target
install(TARGETS vaultusb_cpp RUNTIME DESTINATION bin)

# Install configuration files
install(FILES 
    ../config.toml
    ../config_raspbian.toml
    ../config_dietpi.toml
    ../config_bookworm.toml
    DESTINATION /opt/vaultusb
)

# Install static files and templates
install(DIRECTORY 
    ../app/static
    ../app/templates
    DESTINATION /opt/vaultusb
)

# Install systemd service
install(FILES 
    ../systemd/vaultusb.service
    DESTINATION /etc/systemd/system
)

# Install networking configuration
install(FILES 
    ../networking/dnsmasq-uap0.conf
    ../networking/dnsmasq-usb0.conf
    ../networking/hostapd.conf
    DESTINATION /etc/vaultusb
)

# Install scripts
install(PROGRAMS 
    ../scripts/setup_networking.sh
    ../scripts/make_cert.sh
    DESTINATION /usr/local/bin
)

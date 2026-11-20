cmake_minimum_required(VERSION 3.10)

project(Launcher)

if (WIN32)
    message(STATUS "MSVC -> forcing use of statically-linked runtime.")
    STRING(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE})
    STRING(REPLACE "/MDd" "/MTd" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
endif(WIN32)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT)

# Las cadenas visibles llevan acentos y signos (UTF-8) en el codigo fuente.
if (MSVC)
    add_compile_options(/utf-8)
endif()

file(GLOB source_files "src/*.cpp" "src/*/*.cpp" "src/*/*.hpp" "include/*.h"  "include/*/*.h" "include/*/*/*.h" "include/*.hpp"  "include/*/*.hpp" "include/*/*/*.hpp")
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(${PROJECT_NAME} ${source_files})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "MR-Launcher")

if (WIN32)
    find_package(ZLIB REQUIRED)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto ws2_32 crypt32 httplib::httplib nlohmann_json::nlohmann_json)
elseif (UNIX)
    find_package(ZLIB REQUIRED)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto)
else(WIN32) #MINGW
    add_definitions("-D_WIN32_WINNT=0x0600")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os -s --static")
    target_link_libraries(${PROJECT_NAME} ssl crypto ws2_32 ssp crypt32 z)
endif(WIN32)
target_include_directories(${PROJECT_NAME} PRIVATE "include")

# Pruebas unitarias del modulo de identidad (solo fuentes puras: no se enlaza el launcher).
option(MR_BUILD_TESTS "Compilar MR-Launcher-tests" ON)
if (MR_BUILD_TESTS)
    find_package(doctest CONFIG REQUIRED)
    find_package(OpenSSL REQUIRED)
    add_executable(MR-Launcher-tests
            test/test_main.cpp
            test/test_identity.cpp
            test/test_servers.cpp
            test/test_mods.cpp
            test/test_channel.cpp
            src/MRIdentity.cpp
            src/MRServers.cpp
            src/MRMods.cpp
            src/MRChannel.cpp)
    target_include_directories(MR-Launcher-tests PRIVATE "include")
    target_link_libraries(MR-Launcher-tests PRIVATE
            doctest::doctest nlohmann_json::nlohmann_json OpenSSL::Crypto)
    if (WIN32)
        target_link_libraries(MR-Launcher-tests PRIVATE ws2_32 crypt32)
    endif()
endif()

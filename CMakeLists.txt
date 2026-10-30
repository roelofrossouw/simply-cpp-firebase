cmake_minimum_required(VERSION 3.22)
set(SC_MODULE firebase)
project(simply-cpp-${SC_MODULE} HOMEPAGE_URL "https://github.com/roelofrossouw/simply-cpp-firebase" LANGUAGES CXX)
include(cmake/sc_bootstrap.cmake)

option(SC_FIREBASE_BUILD_TESTS "Build simply-cpp-firebase tests" ${PROJECT_IS_TOP_LEVEL})

get_sc_version()
message(STATUS "SimplyCPP ${SC_MODULE} Version: ${SC_VERSION}")

# sc::rest (libcurl) for the requests, sc::base64 and sc::params; nlohmann/json for the values;
# OpenSSL to sign the service account's token requests (RS256).
find_or_install_package(sc-core simply-cpp-dev simply-cpp 1.24.0)
find_or_install_package(nlohmann_json nlohmann-json3-dev nlohmann_json)
find_or_install_package(OpenSSL libssl-dev openssl)

set(SOURCE_OBJECTS)
set(SOURCE_LIBRARIES)
set(SOURCE_LINK_LIBRARIES)

add_sc_object(sc_firebase
        LINK_LIBRARIES sc::sc-core nlohmann_json::nlohmann_json OpenSSL::Crypto
        PUBLIC_LINK_LIBRARIES sc::sc-core nlohmann_json::nlohmann_json OpenSSL::Crypto)

add_sc_libraries(DESCRIPTION "simply-cpp Google Firebase: service-account access tokens and the Realtime Database")
target_compile_features(sc-firebase PUBLIC cxx_std_20)
target_compile_features(sc-firebase-shared PUBLIC cxx_std_20)

install_sc_module(PATH_VARS CMAKE_INSTALL_INCLUDEDIR)
install(FILES cmake/SimplyCppFunctions.cmake cmake/sc_bootstrap.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sc-firebase
        COMPONENT development)

if (SC_FIREBASE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests sc-firebase-test)
endif ()

add_sc_example(sc-firebase-demo)

package_sc_module(DEV_DEPENDS "simply-cpp-dev (>= 1.24.0~)" nlohmann-json3-dev libssl-dev)

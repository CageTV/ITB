cmake_minimum_required(VERSION 3.21)

# ITBWildlife.dll for Skyrim VR and 1.6.1130 and older: CharmedBaryon's CommonLibSSE-NG through vcpkg, the same sources as the SE/AE build in ../ng.cmake.
project(ITBWildlife VERSION 0.1.1 LANGUAGES CXX)

find_package(CommonLibSSE CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME} NAME "ITBWildlife" AUTHOR "CageTV" SOURCES
    ../src/main.cpp
    ../src/Birds.cpp
    ../src/Spawner.cpp
    ../src/Flee.cpp
    ../src/FleeSettings.cpp
    ../src/Rules.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_definitions(${PROJECT_NAME} PRIVATE ITB_LEGACY)
target_include_directories(${PROJECT_NAME} PRIVATE ../src)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
target_precompile_headers(${PROJECT_NAME} PRIVATE ../src/PCH.h)
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/Z7>)
target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)

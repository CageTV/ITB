# ITBWildlife.dll on alandtse's CommonLibSSE-NG; included by E:/WorkSpace/ng-build/CMakeLists.txt (see ng_plugin there).
ng_plugin(TARGET ITBWildlife NAME ITBWildlife VERSION 0.1.1 AUTHOR "CageTV" ROOT "${CMAKE_CURRENT_LIST_DIR}"
    SOURCES src/main.cpp src/Birds.cpp src/Spawner.cpp src/Flee.cpp src/FleeSettings.cpp src/Rules.cpp
    INCLUDES "${CMAKE_CURRENT_LIST_DIR}/src"
    PCH "${CMAKE_CURRENT_LIST_DIR}/src/PCH.h"
    LIBS nlohmann_json::nlohmann_json)

cmake_minimum_required(VERSION 3.13)

project(OsLaba2Var2Server)

add_executable(OsLaba2Var2Server
        main.cpp
        )

set_target_properties(OsLaba2Var2Server
        PROPERTIES
            CXX_STANDARD 17
            LINK_SEARCH_START_STATIC 1
            LINK_SEARCH_END_STATIC 1
        )

target_link_libraries(OsLaba2Var2Server
        PRIVATE
            OsLaba2Var2Common
            -static-libstdc++
            -static-libgcc
            -static -pthread
        )


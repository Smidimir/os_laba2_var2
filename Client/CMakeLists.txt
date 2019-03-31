cmake_minimum_required(VERSION 3.13)

project(OsLaba2Var2Client)

add_executable(OsLaba2Var2Client
        main.cpp
        )

set_target_properties(OsLaba2Var2Client
        PROPERTIES
            CXX_STANDARD 17
        )

target_link_libraries(OsLaba2Var2Client
        PRIVATE
            OsLaba2Var2Common
            -static-libstdc++
            -static-libgcc
            -static -pthread
        )

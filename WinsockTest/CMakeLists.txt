cmake_minimum_required(VERSION 3.13)

project(WinsockTest)

add_executable(WinsockTest
        main.cpp
        )

set_target_properties(WinsockTest
        PROPERTIES
            CXX_STANDARD 17
        )

target_link_libraries(WinsockTest
        PRIVATE
            OsLaba2Var2Common
            -static-libstdc++
            -static-libgcc
            -static -pthread
        )


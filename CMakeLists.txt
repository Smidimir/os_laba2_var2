cmake_minimum_required(VERSION 3.13)

project(OsLaba2Var2)

set(CMAKE_BUILD_TYPE Release)

add_subdirectory(Common)
add_subdirectory(Client)
add_subdirectory(Server)
add_subdirectory(WinsockTest)

set_target_properties( OsLaba2Var2Client
        PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
        )

set_target_properties( OsLaba2Var2Server
        PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
        )

set_target_properties( WinsockTest
        PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
        )
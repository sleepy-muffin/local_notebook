cmake_minimum_required(VERSION 3.31)

project(felis_bell VERSION 0.1.0 LANGUAGES CXX)


add_library(${PROJECT_NAME} STATIC
    database.cpp
    message.cpp
    local_tools.cpp
    client.cpp
    store.cpp
    tools.cpp
    web_tools.cpp
)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        felis::common
        felis::net
        felis::sqlite3
)

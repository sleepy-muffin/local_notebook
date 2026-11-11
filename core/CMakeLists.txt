# ========== ---------- ---------- ---------- ==========
#                         SET UP
# ========== ---------- ---------- ---------- ==========


find_package(SQLite3 REQUIRED)
find_package(Boost REQUIRED COMPONENTS json regex url)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# The application core. Only `include/` is public: the UI talks to the core
# through `local_notebook/core/interface.hpp` and never sees a store, network or
# executor type.
local_notebook_add_module(core STATIC
        SOURCES
        src/interface.cpp
        src/application.cpp
        src/utils.cpp

        src/utils/context.cpp
        src/utils/utf8.cpp

        src/store/database.cpp
        src/store/managers.cpp

        src/net/protocol.cpp
        src/net/tools.cpp
        src/net/client.cpp

        PUBLIC_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/include

        PRIVATE_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${SQLite3_INCLUDE_DIRS}

        PUBLIC
        # The tests (and the UI's loopback-server test) include Boost.Asio
        # directly. Keeping the Boost targets public makes their include
        # directories available to those consumers; on Windows the headers live
        # in the vcpkg tree rather than the compiler's default include path.
        Boost::json

        PRIVATE
        shark::common
        shark::net
        shark::sqlite3
        ${SQLite3_LIBRARIES}
        Threads::Threads
)

# The TLS client loads the OS root store on Windows so HTTPS works without a
# bundled CA file.
if(WIN32)
    target_link_libraries(local_notebook_core PRIVATE crypt32)
endif()

# ========== ---------- ---------- ---------- ==========
#                          TEST
# ========== ---------- ---------- ---------- ==========


# Regression tests for the public core API.
add_subdirectory(tests)

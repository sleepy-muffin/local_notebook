# Pure payload checks for the request builders: no database, network or display
# server, so they run in the ordinary test suite.
add_executable(
    request-shape-check
    request_shape_check.cpp
)

target_link_libraries(
    request-shape-check
    PRIVATE
    felis_bell
)

add_test(NAME request-shape COMMAND request-shape-check)

# Live probe against a running OpenAI-compatible endpoint. Deliberately not an
# `add_test`: the offline suite must not depend on a server being up.
add_executable(
    live-chat-smoke
    live_chat_smoke.cpp
)

target_link_libraries(
    live-chat-smoke
    PRIVATE
    felis_bell
)

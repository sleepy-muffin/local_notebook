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

# Two clients on one assistant: the second must refuse through `open_error()`
# rather than throw or die. Needs a writable directory, nothing else.
add_executable(
    reopen-conflict-check
    reopen_conflict_check.cpp
)

target_link_libraries(
    reopen-conflict-check
    PRIVATE
    felis_bell
)

add_test(NAME reopen-conflict COMMAND reopen-conflict-check)

# Network tools: a refused loopback connection must be returned to the model as
# a tool result, not thrown, and the stop token must reach the transport. Needs
# no server: 127.0.0.1:1 refuses immediately.
add_executable(
    tool-web-check
    tool_web_check.cpp
)

target_link_libraries(
    tool-web-check
    PRIVATE
    felis_bell
)

add_test(NAME tool-web COMMAND tool-web-check)

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

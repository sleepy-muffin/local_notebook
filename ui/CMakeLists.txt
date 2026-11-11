# ========== ---------- ---------- ---------- ==========
#                         SET UP
# ========== ---------- ---------- ---------- ==========


# Qt 6.8 is the minimum this project builds against: the transcript uses
# QTextListFormat::start(), which older Qt does not have, and the release
# workflow ships 6.8.3 on every platform.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)

# The application uses Qt Widgets without moc (no Q_OBJECT anywhere), so AUTOMOC
# stays off and the build is a plain C++23 compile.
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC ON)

# The transcript renderer is UI-specific: its backend and canvas are Qt types,
# while the layout core stays Qt-free so its geometry tests can link it alone.
add_subdirectory(render)

set(LOCAL_NOTEBOOK_UI_SOURCES
        src/settings/settings.cpp
        src/support/i18n.cpp

        src/ui/AssistantForm.cpp
        src/ui/AssistantPicker.cpp
        src/ui/BackgroundWidget.cpp
        src/ui/ConversationController.cpp
        src/ui/ConversationView.cpp
        src/ui/MainWindow.cpp
        src/ui/MessageDocument.cpp
        src/ui/ScrollBar.cpp
        src/ui/SessionEditor.cpp
        src/ui/SessionItemDelegate.cpp
        src/ui/SessionListModel.cpp
        src/ui/SettingsWindow.cpp
        src/ui/StatusNotice.cpp
        src/ui/SummaryReviewDialog.cpp
        src/ui/TasksWindow.cpp
        src/ui/TranscriptCanvas.cpp
        src/ui/theme.cpp

        ${CMAKE_CURRENT_SOURCE_DIR}/../assets/local_notebook.qrc
)

# The Qt-facing half of the application: widgets, the settings layer and the
# compiled-in resources. Everything it exports is reachable as
# `local_notebook::ui`.
local_notebook_add_module(ui STATIC
        SOURCES
        ${LOCAL_NOTEBOOK_UI_SOURCES}

        PUBLIC_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/src

        PUBLIC
        local_notebook::core
        local_notebook::render
        shark::common
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

# ========== ---------- ---------- ---------- ==========
#             WAYLAND TOPLEVEL ICONS (LINUX)
# ========== ---------- ---------- ---------- ==========

# Qt 6.8's Wayland platform plugin does not implement xdg-toplevel-icon-v1, so
# QApplication::setWindowIcon never reaches the compositor there (see
# src/wayland_icon.cpp). The application talks to the compositor directly when
# libwayland-client is available; builds without it simply keep the icon Qt
# provides. The protocol files were generated once with wayland-scanner 1.23.1
# from the staging xdg-toplevel-icon-v1.xml and are checked in, so no scanner
# is needed to build.
#
# QNativeInterface::QWaylandApplication, the only public way to reach the
# compositor's wl_display without QtWayland's private headers, has been in
# QtGui since 6.5, well below this project's 6.8 minimum.
set(LOCAL_NOTEBOOK_WAYLAND_ICON_SOURCES)
set(LOCAL_NOTEBOOK_WAYLAND_ICON_LIBRARIES)
set(LOCAL_NOTEBOOK_WAYLAND_ICON_DEFINITIONS)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)

    if(PkgConfig_FOUND)
        pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)

        if(WAYLAND_CLIENT_FOUND)
            enable_language(C)
            set(LOCAL_NOTEBOOK_WAYLAND_ICON_SOURCES
                    src/wayland/xdg-toplevel-icon-v1-protocol.c
            )
            set(LOCAL_NOTEBOOK_WAYLAND_ICON_LIBRARIES PkgConfig::WAYLAND_CLIENT)
            set(LOCAL_NOTEBOOK_WAYLAND_ICON_DEFINITIONS LNB_HAVE_WAYLAND_ICON)

            # glibc before 2.34 keeps shm_open in librt; the shared memory
            # fallback in src/wayland_icon.cpp needs it there.
            find_library(LOCAL_NOTEBOOK_RT_LIBRARY rt)
            if(LOCAL_NOTEBOOK_RT_LIBRARY)
                list(APPEND LOCAL_NOTEBOOK_WAYLAND_ICON_LIBRARIES
                        ${LOCAL_NOTEBOOK_RT_LIBRARY})
            endif()
        endif()
    endif()
endif()

# ========== ---------- ---------- ---------- ==========
#                           UI
# ========== ---------- ---------- ---------- ==========

# The desktop executable. It owns main() plus the Wayland icon helper and links
# the UI submodule, exposed to the rest of the build as `local_notebook::app`.
add_executable(local_notebook_app
        src/main.cpp
        src/wayland_icon.cpp
        ${LOCAL_NOTEBOOK_WAYLAND_ICON_SOURCES}
)
add_executable(local_notebook::app ALIAS local_notebook_app)

target_link_libraries(local_notebook_app
        PRIVATE
        local_notebook::ui
        ${LOCAL_NOTEBOOK_WAYLAND_ICON_LIBRARIES}
)

target_compile_definitions(local_notebook_app
        PRIVATE
        ${LOCAL_NOTEBOOK_WAYLAND_ICON_DEFINITIONS}
)

set_target_properties(local_notebook_app PROPERTIES
        OUTPUT_NAME "local_notebook-${PLATFORM}-${ARCH}"
)

# ========== ---------- ---------- ---------- ==========
#                          TOOLS
# ========== ---------- ---------- ---------- ==========

# Development-only executable used to regenerate README screenshots.
add_executable(local_notebook_screenshots
        tools/screenshots.cpp
)

target_link_libraries(local_notebook_screenshots
        PRIVATE
        local_notebook::ui
)

# ========== ---------- ---------- ---------- ==========
#                         CTESTS
# ========== ---------- ---------- ---------- ==========

# Regression tests for the UI-side send policy.
# The controller is Qt-free: it is the send policy between the widgets and the
# public core API, so it can be exercised without a display server.
add_executable(
        conversation-controller-test
        tests/conversation_controller_test.cpp
)

target_link_libraries(
        conversation-controller-test
        PRIVATE
        local_notebook::ui
)

add_test(NAME conversation-controller COMMAND conversation-controller-test)

# Markdown conversion runs on the UI thread, but its data model is independent
# of widgets and can be tested without a display server.
add_executable(
        message-document-test
        tests/message_document_test.cpp
)

target_link_libraries(
        message-document-test
        PRIVATE
        local_notebook::ui
)

add_test(NAME message-document COMMAND message-document-test)

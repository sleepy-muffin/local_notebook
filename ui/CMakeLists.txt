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
        src/support/LlmPayloadFormat.cpp
        src/support/LogFormat.cpp
        src/support/KnownModels.cpp
        src/support/LogPayloadBridge.cpp
        src/support/OpenFailure.cpp
        src/support/ReasoningText.cpp

        src/ui/AssistantForm.cpp
        src/ui/AssistantPicker.cpp
        src/ui/AssistantRowDelegate.cpp
        src/ui/BackgroundWidget.cpp
        src/ui/ChevronStyle.cpp
        src/ui/ConversationController.cpp
        src/ui/ConversationView.cpp
        src/ui/LogInspectorWindow.cpp
        src/ui/LogRowDelegate.cpp
        src/ui/MainWindow.cpp
        src/ui/MessageDocument.cpp
        src/ui/ScrollBar.cpp
        src/ui/SessionEditor.cpp
        src/ui/SessionItemDelegate.cpp
        src/ui/SessionListModel.cpp
        src/ui/SettingsWindow.cpp
        src/ui/StatusNotice.cpp
        src/ui/TimeFormat.cpp
        src/ui/TranscriptCanvas.cpp
        src/ui/theme.cpp
)

# The Qt-facing half of the application: widgets, the settings layer and the
# compiled-in resources. Everything it exports is reachable as
# `local_notebook::ui`.
add_library(ui STATIC
        ${LOCAL_NOTEBOOK_UI_SOURCES}
)

add_library(local_notebook::ui ALIAS ui)

target_include_directories(ui
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(ui
        PUBLIC
        felis_bell
        render
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
        # The compiled-in resources (chrome icons, app icon, theme, licences).
        # AUTORCC only processes .qrc files that are listed in a target's
        # sources; without this the :/icons, :/appicon and :/licenses trees are
        # never embedded and every resource_icon() call returns a null icon.
        ${CMAKE_CURRENT_SOURCE_DIR}/../assets/local_notebook.qrc
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
        # Keep the reported version in step with the one CMake owns, so the
        # executable and the release artefacts never disagree.
        LOCAL_NOTEBOOK_VERSION="${PROJECT_VERSION}"
)

set_target_properties(local_notebook_app PROPERTIES
        OUTPUT_NAME "local_notebook-${PLATFORM}-${ARCH}"
)

# Windows needs two executable-level details the other platforms do not:
#
#   * A GUI subsystem image. `add_executable` with no `WIN32_EXECUTABLE` links a
#     console program, so launching the app pops a terminal window behind it.
#     Switching the subsystem makes the linker look for WinMain instead of main,
#     which Qt6::EntryPoint supplies.
#   * An embedded icon. QApplication::setWindowIcon only sets the title-bar / WM
#     icon; the file icon Explorer draws and the icon a pinned or grouped
#     taskbar button uses come from the executable's resource table, so the .ico
#     is compiled into the image through a generated resource script.
if(WIN32)
    enable_language(RC)

    get_filename_component(LNB_APP_ICON
            "${CMAKE_CURRENT_SOURCE_DIR}/../assets/appicon/app.ico" ABSOLUTE)
    file(TO_NATIVE_PATH "${LNB_APP_ICON}" LNB_APP_ICON)
    configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/../assets/appicon/local_notebook.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/local_notebook.rc"
            @ONLY
    )

    target_sources(local_notebook_app PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/local_notebook.rc")
    target_link_libraries(local_notebook_app PRIVATE Qt6::EntryPoint)
    set_target_properties(local_notebook_app PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# ========== ---------- ---------- ---------- ==========
#                      SCREENSHOTS
# ========== ---------- ---------- ---------- ==========

# Renders the windows README.md and docs/architecture.md embed. It seeds a scratch
# store, builds the real widgets and saves what they paint, so it needs neither a
# display server nor an endpoint. Kept out of the default build: run it by name.
add_executable(local_notebook_screenshots
        tools/screenshots.cpp
        # The qrc has to sit on this target too, or the :/icons, :/appicon and
        # :/licenses trees are never embedded and the icons come out blank.
        ${CMAKE_CURRENT_SOURCE_DIR}/../assets/local_notebook.qrc
)

target_link_libraries(local_notebook_screenshots
        PRIVATE
        local_notebook::ui
)

target_compile_definitions(local_notebook_screenshots
        PRIVATE
        LOCAL_NOTEBOOK_VERSION="${PROJECT_VERSION}"
)

set_target_properties(local_notebook_screenshots PROPERTIES
        EXCLUDE_FROM_ALL TRUE
)

# ========== ---------- ---------- ---------- ==========
#                         CTESTS
# ========== ---------- ---------- ---------- ==========

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

# Request payload rendering is pure text production, so it runs without a display
# server and checks the elision of opaque reasoning blobs in particular.
add_executable(
        llm-payload-format-test
        tests/llm_payload_format_test.cpp
)

target_link_libraries(
        llm-payload-format-test
        PRIVATE
        local_notebook::ui
)

add_test(NAME llm-payload-format COMMAND llm-payload-format-test)

#pragma once

// A QProxyStyle that replaces the platform's small arrow glyphs (combo boxes,
// spin boxes) with a thin, antialiased chevron drawn from the active palette.
//
// The platform styles draw those arrows with their own hard-coded pixmaps: on
// Breeze they are small and chunky, and they do not follow the theme's text
// colour because they are not palette-aware in the way the rest of the chrome
// is. A stylesheet cannot fix this — Qt stylesheets have no palette-derived
// `image` and no CSS triangle — so the arrows are painted here instead.

class QApplication;

namespace ui {

// Installs the chevron style on `app` unless it is already installed. The
// platform style is copied first (a fresh instance of the same style), so the
// proxy never shares ownership of the application's previous style object.
void install_chevron_style(QApplication& app);

} // namespace ui

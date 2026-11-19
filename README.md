# Local Notebook

An AI client optimized to be high-performance and comfortable to be used.

[![License: MIT](https://img.shields.io/badge/license-MIT-cba6f7.svg)](LICENSE.txt)
[![Latest release](https://img.shields.io/github/v/release/sleepy-muffin/local_notebook?color=f5c2e7)](../../releases/latest)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-cba6f7.svg)](#download)
[![Built with Qt 6](https://img.shields.io/badge/Qt-6-cba6f7.svg)](https://qt.io)

![Local Notebook](docs/screenshots/main-window-light.png)

---

## Download

| Platform | File |
|---|---|
| **Linux** (x86_64) | [`local_notebook-*-linux-x86_64.AppImage`](../../releases/latest) |
| **Windows** (x86_64) | [`local_notebook-*-windows-x86_64.zip`](../../releases/latest) |

---

Local Notebook is a desktop notebook for talking to a local or self-hosted model.
It runs one **assistant** per process across many **sessions**, and keeps long
conversations coherent with a **session summarization plus a shared assistant
memory** that you can open, edit and regenerate yourself.

Supports OpenAI-compatible endpoint local or remote, with optional SOCKS5
HTTP-CONNECT proxy support. Both the Responses API and the Chat Completions API
are spoken, and each assistant picks the one its endpoint implements.

Currently, supports 5 UI languages: English, 简体中文, 繁體中文, 日本語 and Deutsch.

Supports tools:
- Searching online
- Reading (plain and regex search), Writing files in the configured shared directories

---

## Build from source

Requires CMake 3.31+, Qt 6.8+ and a C++23 compiler.

```sh
git clone --recurse-submodules https://github.com/sleepy-muffin/local_notebook.git
cmake --preset linux-x64-release
cmake --build build/linux-x64-release -j
```

`ctest --test-dir build/linux-x64-release` runs the tests. The screenshots this
README uses are rendered by `local_notebook_screenshots <output-directory>`,
which needs neither a display server nor an endpoint.

---

## License

MIT — see [LICENSE.txt](LICENSE.txt).

# Contributing to RPG Forge

Thank you for your interest in contributing to RPG Forge! We welcome contributions from everyone, whether it's bug reports, feature suggestions, or code changes.

## 📜 Code of Conduct

As a KDE-based project, we follow the [KDE Community Code of Conduct](https://kde.org/code-of-conduct/). Please be respectful and professional in all interactions.

## 🛠️ Development Setup

### Dependencies
Ensure you have the following installed (package names vary by distro):
- **Qt 6** (Widgets, Core, Gui, WebEngine)
- **KDE Frameworks 6 (KF6)** (CoreAddons, I18n, XmlGui, TextEditor, KIO, WidgetsAddons, Parts)
- **libgit2**
- **libcmark-gfm**

### Building
Use the project's build script to ensure correct environment paths:
```bash
./build.sh
```

## ⌨️ Coding Standards

To keep the codebase maintainable and consistent with the KDE ecosystem:

- **Style:** We follow the standard KDE coding style.
- **Indentation:** Use **4 spaces** (no tabs).
- **Naming:** `camelCase` for functions and variables; `PascalCase` for classes.
- **Include Guards:** Use standard `#ifndef GUARD_H` / `#define GUARD_H` patterns.
- **Licensing:** Every new `.cpp` or `.h` file **MUST** include the GPL-3.0 license header found in existing files.
- **Modern C++:** Use C++17 features where appropriate.

## 🚀 The Pull Request Process

1.  **Fork and Branch:** Create a fork and create a descriptive feature branch.
2.  **Commit Messages:** Write clear, descriptive commit messages. Explain *why* you made a change, not just *what* changed.
3.  **Stay Updated:** Before submitting, rebase your branch on the latest `main`.
4.  **Submit:** Open a Pull Request on GitHub. Be prepared for a friendly code review!

## ⚖️ License

By contributing to RPG Forge, you agree that your contributions will be licensed under the **GNU General Public License v3.0 or later**.

# RPG Forge

RPG Forge is a KDE-native IDE specifically designed for RPG game designers. It integrates markdown editing, project management, version control, and LLM-powered game analysis into a single application.

## Features (In Progress)
- **Embedded Kate Editor:** Powerful text editing with markdown support.
- **Outline Panel:** Real-time heading structure for navigation.
- **Live Preview:** Real-time rendered markdown with LaTeX and CSS support.
- **Variable System:** Define, reference, and auto-complete custom variables in documents.
- **Git Integration:** Built-in version control (Phase 6).
- **LLM Game Analyzer:** AI-powered analysis for rule conflicts and ambiguities (Phase 10).

## Building and Running
To build RPG Forge, you will need the following dependencies:
- Qt 6 (Widgets, Core, Gui, WebEngine)
- KF6 (CoreAddons, I18n, XmlGui, TextEditor, KIO, WidgetsAddons, Parts)
- libgit2
- libcmark-gfm

Use the provided build script:
```bash
./build.sh
```
To run the application:
```bash
./build/bin/rpgforge
```

## License
RPG Forge is released under the **GNU General Public License v3.0 or later**. See the [LICENSE](LICENSE) file for the full text.

Copyright (C) 2026 Sheldon L.

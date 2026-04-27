# Mythos Design System — "The Worldbuilder's Desk"

**Version:** 1.0  
**Objective:** Define a high-end, premium aesthetic for a writer's IDE, focusing on focus, tactile elegance, and mythical atmosphere.

## 1. Color Palettes

### 1.1 Core Mode: "Mythos" (Primary Dark Mode)
A high-contrast, deep charcoal workspace with metallic accents.
- **Charcoal (Primary Background):** `#121216`
- **Deep Slate (Secondary/Panels):** `#1E1E24`
- **Brass (Primary Accent/Secondary Text):** `#B5A642`
- **Gold (Primary Text/Highlights):** `#D4AF37`
- **Vellum (Editor Background):** `#18181D` (A slightly warmer charcoal)
- **Ink (Primary Editor Text):** `#E0D8C0` (Warm off-white/gold-tinted)

### 1.2 Focus Mode: "Lumina" (Light/Glassmorphism)
A bright, airy space for pure writing flow.
- **Background:** Soft ethereal white-blue glow (`#F0F4F8` base)
- **Editor Pane:** Frosted glass (Translucent white, `backdrop-filter: blur(20px)`)
- **Text:** Deep Charcoal (`#121216`)
- **Accents:** Subtle Gold (`#D4AF37`)

## 2. Typography

### 2.1 Headings & Narrative Text
- **Typeface:** **EB Garamond** (Serif)
- **Weights:** Regular, Semibold, Bold
- **Rationale:** Classic, high-end book aesthetic. Perfect for worldbuilding and manuscript writing.

### 2.2 UI Elements (Buttons, Menus, Status)
- **Typeface:** **Inter** or **SF Pro** (Sans-serif)
- **Weights:** Medium, Semibold
- **Rationale:** Modern, clean, and highly legible at small sizes.

### 2.3 Monospace (Metadata, Code, Tags)
- **Typeface:** **JetBrains Mono**
- **Rationale:** Clear distinction for technical data or markdown source.

## 3. UI Components & Layout

### 3.1 The Chronicle (Primary Sidebar)
- **Width:** Very slim (48px - 64px).
- **Style:** Deep Slate background.
- **Icons:** Thin-stroke gold (`#D4AF37`) icons. Active icon has a subtle brass glow.
- **Actions:** Library (Files), Story Map (Explorations), Codex (Variables), Muse (AI Chat), Ledger (Settings).

### 3.2 The Manuscript (Main Editor)
- **Framework:** TipTap (React).
- **Layout:** Centered column (Max-width: 800px). Generous margins (100px+).
- **Line Height:** 1.6 (Writer-centric focus).
- **Features:** 
    - Floating contextual "Muse Menu" for AI actions.
    - Inline `@Character` mentions highlighted in subtle gold.
    - Section folding indicated by gold glyphs in the gutter.

### 3.3 The Story Map (Visual History)
- **Style:** A vertical timeline integrated into the "Explorations" panel.
- **Visuals:** Gold paths on charcoal. Milestones are gold circles. Landmarks are brass ribbon icons.
- **Interactive:** Nodes are clickable to preview or switch paths.

### 3.4 The Ledger (Status Bar)
- **Style:** Thin bar at the bottom. Gold text on Deep Slate.
- **Metadata:** Word Count, Active Exploration, Sync Status (Cloud/Git), AI Model status.

## 4. Interaction Principles

- **Tactile Transitions:** Use subtle, slow-fade transitions (300ms) for panel switches.
- **Floating Controls:** Avoid heavy toolbars. Use floating menus and context-aware popovers (Muse Menu).
- **Depth:** Use box-shadows with gold-tinted glows instead of borders for high-end feel.
- **Lumina Toggle:** A distinct, smooth transition where the charcoal UI "melts" into the glassmorphism focus mode.

---

## 5. Reference Mockups
- **Mythos Core:** `nanobanana-output/a_highfidelity_ui_mockup_for_a_p.png`
- **Lumina Focus:** `nanobanana-output/a_highfidelity_ui_mockup_for_the.png`

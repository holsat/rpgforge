# RPG Forge — Build Plan

## Overview

RPG Forge is a KDE-native IDE for RPG game designers. It combines a powerful markdown editor (built on Kate/KTextEditor), project management, version control, a variable/formula system, LLM-powered analysis, and rules simulation into a single integrated application.

**Tech Stack:**
- **Language:** C++17 with Qt6 / KDE Frameworks 6
- **Editor:** KTextEditor (Kate component)
- **Markdown parsing:** cmark-gfm (C library) + custom extensions for variables
- **Preview:** QWebEngineView rendering HTML (CSS/LaTeX via KaTeX.js)
- **Git:** libgit2
- **LLM:** Async HTTP client hitting OpenAI-compatible APIs (covers cloud + Ollama)
- **Build:** CMake + KDE ECM (Extra CMake Modules)
- **Diff:** libgit2 diff / Myers algorithm

---

## Phase 1 — Skeleton App with Embedded Kate Editor
**Status:** Complete

## Phase 2 — Document Outline Panel + Breadcrumb Navigation
**Status:** Complete

## Phase 3 — Live Split-Pane Markdown Preview (HTML/CSS)
**Status:** Complete

## Phase 4 — Variable and Formula System
**Status:** Complete

## Phase 5 — Basic Project Management
**Status:** Complete

## Phase 6 — Git Integration (LibGit2)
**Status:** Complete

## Phase 7 — Visual Diff and Version History
**Status:** Complete

## Phase 8 — PDF Export (QtPDF / WebEngine)
**Status:** Complete

## Phase 9 — LLM Foundation & Inline Actions
**Status:** Complete

## Phase 10 — LLM Game Analyzer (Inline Diagnostics & RAG)
**Status:** Complete

---

## Phase 11 — Onboarding Wizard & LLM Validation

**Status:** Complete

**Goal:** A multi-step QWizard to guide new users through project creation, AI configuration, and GitHub setup.

**Key Deliverables:**
- `OnboardingWizard` class managing project identity, AI, and Cloud settings.
- Real-time LLM connection testing ("Test Connection" button).
- Dynamic model fetching from OpenAI and Ollama.
- Automated Ollama model pulling with progress feedback.
- Post-wizard auto-opening of the project README.

---

## Phase 12 — Data Import (Scrivener & Word)

**Status:** Complete

**Goal:** Enable migration from Scrivener and Word/RTF into RPG Forge with media preservation.

**Key Deliverables:**
- `DocumentConverter` utility leveraging Pandoc for RTF/DOCX/PDF to Markdown conversion.
- `ScrivenerImporter` for parsing `.scrivx` binders and mapping them to RPG Forge structures.
- Centralized media handling with sanitized filename prefixing to avoid collisions.
- EMF/WMF to SVG conversion for native preview support.
- Preservation of advanced styling via `gfm+raw_html`.

---

## Phase 13 — Rules Simulation Engine (Core)

**Status:** Complete

**Goal:** Define and run basic game simulations using LLM-as-GM with a logic-first architecture.

**Key Deliverables:**
- `DiceEngine` for cryptographically secure RPG rolls.
- `SimulationState` for schema-less dynamic world tracking.
- Multi-agent architecture (Arbiter, Griot, and independent Actors).
- `SimulationPanel` dashboard with live log and state inspector.
- Monte-Carlo batch mode for mechanical stress testing.
- MCP Server interface to expose rules and state to external agents.

---

## Phase 14 — Multi-Agent Simulation (Advanced)

**Status:** Not Started

**Goal:** Player sub-agents that make autonomous decisions during simulation.

---

## Phase 15 — Character Generator

**Status:** Not Started

---

## Phase 16 — MCP Interface & Project Bridge

**Status:** Not Started

**Goal:** Expose project rules and simulation state via Model Context Protocol.

---

## Phase 17 — Polish and Advanced Features

**Status:** Not Started

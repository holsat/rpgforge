# RPG Forge

**RPG Forge** is a writer-centric workspace designed for roleplaying game designers, worldbuilders, and novelists. It combines a powerful writing environment with intelligent organization and an automated assistant that helps you stay consistent across even the most complex projects.

## 🚀 Key Capabilities

### 1. Writer-Centric Project Management
Organize your project the way you think, not the way a computer stores files:
*   **Manuscript:** The heart of your creation. Documents here are automatically assembled into your final work. RPG Forge handles the numbering of **Chapters** and the flow of **Scenes** for you, so you can focus on the words.
*   **Research & Lore:** A dedicated space for your world's foundation. Keep track of **Characters**, **Places**, and **Cultures** with specialized views that keep your notes organized and accessible.
*   **Corkboard View:** Visualize your folders as physical cards. Reorder scenes or chapters by simply dragging them across the board, and your project structure updates instantly.

### 2. A Powerful Writing Environment
Authors can stop worrying about anything other than writing. Powered by a flexible and powerful text editor, RPG Forge gives you the tools to immerse yourself in your work:
*   **Focus Mode:** Strip away every distraction with a single shortcut (`Ctrl+Shift+F`) to enter a clean, fullscreen writing space.
*   **Research Split-View:** When you open a note from your Research folder, it automatically appears alongside your manuscript. Reference your lore on the right while you write on the left.
*   **Flexible Formatting:** Use simple Markdown for basic styling, or leverage HTML and CSS if you want complete control over the final look of your document.

### 3. Professional Tracking & Syncing
RPG Forge manages your documents with the same rigor a programmer uses for code, giving you total creative freedom:
*   **Automatic Checkpoints:** Every time you save, RPG Forge creates a "checkpoint" of your work. You can easily retrieve, compare, or merge older versions of any document.
*   **Exploration Branches:** Want to try a completely different direction for a chapter? Create an "Exploration" (branch). You can work in this alternate timeline without affecting your main manuscript, then merge the changes back or discard them later.
*   **Worry-Free Switching:** When you switch between different explorations, RPG Forge automatically saves your current work so you never lose a word.
*   **Data Protection:** Professional versioning protects you from losing your work should anything happen to your computer, allowing you to sync your entire project to remote backups like GitHub.

### 4. Maintain Consistency with Ease
For game designers and worldbuilders, consistency is everything. 
*   **Variable System:** Define stats, names, or values in one place and reference them throughout your text. If you change a character's base health or a city's name, it updates everywhere automatically, ensuring your rules and narrative never drift apart.

### 5. Your Intelligent Writing Aide
RPG Forge includes an integrated assistant that takes mundane project management tasks off your plate:
*   **Context-Aware Assistance:** The assistant has direct knowledge of everything you have written so far. It can offer suggestions, expand on ideas, or rewrite sections—all within the specific context of your unique creation.
*   **Background Synopsis:** As you write, the assistant works in the background to summarize your files and folders, keeping your Corkboard informative without you having to write a single summary.
*   **Narrative & Rule Analyzer:** The background analyzer scans your *entire* manuscript—even if it's broken into dozens of small files—looking for inconsistencies in your rules or narrative logic. It highlights these issues for you so you don't have to keep every detail in your head.

### 6. Privacy, Control, and Frugality
We believe your copyrighted work belongs to you.
*   **Local AI Support:** RPG Forge integrates with **Ollama**, allowing you to run AI models locally on your own hardware. This keeps your data private (it never leaves your machine to train vendor models) and keeps your costs down by avoiding per-token service fees.

---

## 🛠 How It Works

### Setting Up a Project
When you create a **New Project**, RPG Forge sets up a professional structure for you:
*   `/manuscript`: Your story and rules.
*   `/research`: Your lore, characters, and worldbuilding notes.
*   `/stylesheets`: Where your custom design lives.

### Writing and Organizing
*   **Arranging:** The order of files on your Corkboard is the order they will appear in your final document.
*   **Categorizing:** Right-click any folder to set its category (like **Chapter** or **Character**). This changes how the folder behaves and how its contents are compiled.
*   **Linking:** Drag a research note directly into your manuscript to create an instant link.

### Compiling Your Work
When you are ready to share your creation, use the **Compile** function. RPG Forge gathers your manuscript files, applies automatic chapter numbering, resolves all your variables and calculations, and generates a professionally styled PDF.

---

## 🏗 Installation & Building

### Dependencies
*   Qt 6 & KDE Frameworks 6
*   `libgit2`, `cmark-gfm`
*   **Ollama** (for local AI assistant capabilities)
*   Recommended Local Model: `mistral` or `llama3`

### Build Instructions
```bash
./build.sh
./build/bin/rpgforge
```

---

## ⚖ License
RPG Forge is released under the **GNU General Public License v3.0**.

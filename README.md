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
*   **Data Protection:** Professional versioning protects you from losing your work should anything happen to your computer, allowing you to sync your entire project to remote file management systems like GitHub.

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
When you first open the application you will see a filesystem view on the bottom right while stacked on top is an empty project view. There should be a large button that says Create Project and it allows you to create a name for your work. Underneath this project view RPG Forge sets up a professional structure for you:
*   `/manuscript`: Your game book, story and rules.
*   `/research`: Your lore, characters, and worldbuilding notes.
*   `/stylesheets`: Where your custom design lives.

### Writing and Organizing
*   **manuscript:** Underneath the Manuscript folder you can create a number of Chapter folders. Each can be named the name of a chapter and within each you can have one or more documents that represent the scenes or sections of that chapter. You can create as many additional chapter folders as you like. We suggest not naming them Chapter 1, Chapter 2, etc. as the app will do this for you when you export it to a particular format. Instead we suggest that you name them the name of the Chapter. The order in which they will appear in the preview view or in the final manuscript will depend on their arrangement in the Manuscript Corkboard.
*   **Arranging:** Clicking on a folder under the Manuscript folder should bring up a Corkboard view where every chapter folder is arranged in the order in which you want them to appear in your work. THis order can be changed by dragging and dropping the cards around. The sysnopsis on each card is automatically generated for you when you save a file. There is also a corkboard view within each chapter folder that allows you to determine which order the documents in that folder should appear in the output. 
*   **Categorizing:** Right-click any folder to set its category (like **Chapter** or **Character**). This changes how the folder behaves and how its contents are compiled.
*   **Linking:** Drag a research note directly into your manuscript to create an instant link.

### Syncing, Versioning and Saving.
*   RPG Forge treats your manuscript like computer code. Those familiar with software development will recognize that RPG Forge uses git under the hood to version and save your work, both locally on your computer and upstream in the Cloud using a service called Github. THis requires some setup. YOu need a github account and need to create a github access token, which you then need to configure inside of RPG Forge. ONce you have done this, you are able to right click on any manuscript file and retrieve past versions. You can also compare previous versions with each other and merge changes. 
*   **Exploration:** In addition we use something called branching to allow you to explore taking your work in a different direction that initially anticipated. THis way you can go off and explore new ideas without stomping on your existing ones. Should those ideas not work out, you can easily switch your "branch" back to the one you started with, or to a different exploration altogether. 

### Compiling Your Work
When you are ready to share your creation, use the **Compile** function. RPG Forge gathers your manuscript files, applies automatic chapter numbering, resolves all your variables and calculations, and generates a professionally styled PDF.

---

## 🏗 Installation & Building

### Dependencies
*   Qt 6 & KDE Frameworks 6
*   `libgit2`, `cmark-gfm`
*   **Ollama** (for local AI assistant capabilities)
*   Recommended Local Model: `qwen3.5`

### Build Instructions
```bash
./build.sh
./build/bin/rpgforge
```

---

## ⚖ License
RPG Forge is released under the **GNU General Public License v3.0**.

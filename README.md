# RPG Forge — The Intelligent AI Agentic Workspace for Worldbuilders, Game Designers and Authors.

RPG Forge is a modern, professional workspace designed specifically for the complex needs of RPG game designers, novelists, and worldbuilders. It combines a distraction-free writing environment with a powerful, AI-assisted brain that helps you track your lore, analyze your mechanics, and maintain absolute consistency across your entire project.

## Key Features for Creators

### 🏛️ Automated LoreKeeper
Stop losing track of your world's details. As you write your manuscript, the LoreKeeper AI works in the background to identify things you want to keep track of. Characters, settings, cultures, etc. You specify to the LoreKeeper what you want it to track and write a prompt/description for each item to track. It then automatically builds a structured "World Bible" for you based on your directions, synthesizing information from your prose into organized dossiers so you always have a quick reference for a character's eye color or the hidden history of a forgotten city.

### 🧠 Semantic AI Assistant
The Forge doesn't just check your spelling; it understands your world. Our integrated Writing Assistant automatically builds a RAG (retrieval augmented generation) AI assistant that can answer questions about your own story or game world, and help you brainstorm plot points that fit your established lore, and even provide real-time feedback on your game rules. It uses "Semantic Search" to instantly find relevant context from thousands of your own notes to ensure every suggestion is grounded in your unique vision. 

### 🧠 AI Provider and Model Agnostc
Our built in AI assistants: the Lorekeeper, the Semantic Assistant and the RAG chatbot, can each be configured to use a different AI provider and model. RPG Forge supports both cloud based or local AI models. Just configure them with your API key and select which provider and model you want to use for each built in AI Agent. RPG Forge supports configuring the following local and cloud-based providers: Ollama, LM Studio (via the server interface) Google Gemini, OpenAI, Anthropic and Grok.

### Variable and Rules Assistant
The Librarian service runs continually in the background as you type, capturing your rules, mechanics and tables, indexing them and storing them in a local database. IT also translates them into local variables that you can use through autocomplete as you type or drag and drop so that you don't accidentally enter conflicting information. Data from these variables are automatically resolved in the life Preview view that updates as you type and in the pdf export function. Don't remember what your unique fanasy race's hit points are? define it in a variable and reuse that variable everywhere to ensure consistency. But even if you don't use the librarian service (which runs automatically) the semantic assistant will find and flag inconsistencies for you, even across different files!

### 🗺️ Authoritative Project Management
Your project structure is the "Source of Truth." The RPG Forge organizes your manuscripts, research material, media, and lore into a logical hierarchy that defines your project. Whether you are reordering scenes to perfect a chapter's flow or importing a complex Scrivener project, the workspace ensures your structure is validated, logged, and safe.

### 🔄 Branching "Explorations"
Writer's block often comes from the fear of making the "wrong" choice. Our Exploration system (with github integration) allows you to explore different paths. Want to see what happens if the protagonist fails their quest? Open an Exploration. You can experiment freely, and if you like the results, you can merge them back into your main story. If not, simply discard the branch and your original work remains untouched.

### 🔄 Version retrieval and comparison
Want an old version of a manuscript, chapter or scene file? RIght click to restore and old version, then open a visual comparison tool that allows you to merge in that changes that matter to you.

### 📊 Rule Simulation & Analysis
For game designers, consistency in mechanics is just as important as consistency in plot. RPG Forge includes tools to analyze your game's variables and formulas. The AI can help you simulate combat encounters or social challenges based on your own ruleset, helping you find balance issues before they ever reach the table.

### 📝 Distraction-Free Professional Writing
Built on high-performance editing technology, RPG Forge provides a clean, distraction-free Markdown writing experience. Enjoy professional typography, powerful search tools, and a flexible interface that lets you hide the technical panels when it's time to focus purely on the words.

## Why RPG Forge?

*   **Privacy First:** Your notes and manuscript remain on your computer. You choose which AI providers to connect to. We are not an QI provider and RPG forge has no access to and can neven use your creative work to train and AI models. If you configure and use cloud based AI providers check with your AI providers regarding their privacy and copyright policies, that is something we have no control over. If you have the ability to run a local model, because you have an apple silicon Mac or a very beefy computer, then your content never leaves your home.
*   **Built for Professional Performance:** A native Linux experience that respects your system theme and provides the responsiveness that serious creators demand.
*   **One Workspace, Total Control:** No more switching between word processors, spreadsheets, and wiki software. No more worrying about saving multiple versions of your files, and then manually merging and chasing down all changes. Everything you need to build a universe, track its' development and apply polish, or simulate play testing is in one place.
*   **Scrivener Import:** The only linux native tool that I know of that will import your scrivener project for you and allow you to continue to work on it on Linux.

---

## 🏗 Installation & Building

### Dependencies
*   Qt 6 & KDE Frameworks 6
*   `libgit2`, `libcmark-gfm`
*   **AI Support:** Supports local (Ollama, LM Studio) and cloud-based (Google Gemini, OpenAI, Anthropic, Grok) providers.

### Build Instructions
```bash
./build.sh
./build/bin/rpgforge
```

---

## ⚖ License
RPG Forge is released under the **GNU General Public License v3.0**.
*Developed with ❤️ for the worldbuilding community.*

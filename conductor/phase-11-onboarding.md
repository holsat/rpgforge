# Phase 11: Onboarding Wizard

## Objective
Create a user-friendly, multi-step onboarding wizard (`QWizard`) that runs the first time the application is launched. It will guide the user through setting up their first project, configuring AI providers, and setting up GitHub integration.

## Proposed Components

### 1. `OnboardingWizard` (New Class inheriting `QWizard`)

#### Page 1: Welcome & Project Identity
- **Content:** A brief welcome message explaining RPG Forge.
- **Inputs:** 
  - Project Name (e.g., "My Epic Campaign").
  - Project Directory (using `KUrlRequester` or similar to pick a folder).
- **Action:** Validates that the directory is valid and empty (or prompts if not).

#### Page 2: AI Configuration
- **Content:** Step-by-step instructions on how to get API keys for different providers.
- **Provider Selection:** A combo box or radio buttons to select the default provider (OpenAI, Anthropic, Ollama).
- **Dynamic Content based on Provider:**
  - **OpenAI:** 
    - Instructions: "Go to platform.openai.com/api-keys, sign in, and create a new Secret Key."
    - Link to OpenAI API keys page.
    - Input: API Key (password masked).
    - Default Model selection.
  - **Anthropic:**
    - Instructions: "Visit console.anthropic.com/settings/keys, create a key, and paste it below."
    - Link to Anthropic Console.
    - Input: API Key (password masked).
    - Default Model selection.
  - **Ollama (Local):**
    - The wizard will attempt to detect if Ollama is installed (e.g., by running `ollama --version` via `QProcess`).
    - If not installed, provide OS-specific instructions:
      - Arch/CachyOS: `sudo pacman -S ollama` and `systemctl enable --now ollama`.
      - General: Link to ollama.com.
    - Input: Local endpoint (default: `http://localhost:11434`).
    - Default Model selection.

#### Page 3: GitHub Integration (Cloud Backup)
- **Content:** Instructions on generating a GitHub Personal Access Token (PAT) with `repo` scope.
- **Link:** Direct link to GitHub token generation pre-filled with scopes.
- **Inputs:**
  - GitHub PAT.
  - Repository Name (auto-populated based on the Project Name).
  - Private/Public toggle (default: Private).
- **Action on Completion:** 
  - Authenticate with GitHub using the provided PAT.
  - Automatically create the repository on GitHub using the GitHub API.
  - Run `git init` in the local project directory.
  - Set the remote origin to the newly created GitHub repository.
  - **CRITICAL:** Do *not* stage or commit any files yet. The project will only track files added to the Manuscript section (to be handled by the existing auto-sync logic, which we will ensure only triggers for Manuscript files).

#### Page 4: Completion
- **Content:** Summary of actions. "Ready to forge your world!"
- **Action:** Clicking "Finish" closes the wizard and creates the project structure.

### 2. `MainWindow` / Application Startup Integration
- Modify the startup logic in `main.cpp` or `MainWindow` constructor.
- Check `QSettings` for a `firstRun` flag or the existence of a `lastProject`.
- If it's the first run, instantiate and execute `OnboardingWizard` modally before showing the main IDE window.

### 3. Project Initialization Logic Refactoring
- Move the hardcoded project template creation logic out of `MainWindow::newProject` and into `ProjectManager` so it can be called seamlessly by the Onboarding Wizard upon completion.

## Next Steps (Phase 11 part 2)
- After this core onboarding is implemented, we will add a second part to the wizard (or a separate tool) for importing and converting existing files into the Manuscript. For now, we focus strictly on initialization.

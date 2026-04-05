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
- **Choice:** Users can choose to create a new project or import an existing Scrivener project.
- **Robust Pathing:** The wizard automatically creates the project directory recursively if it does not exist when the user clicks "Next".

#### Page 2: AI Configuration
- **Content:** Step-by-step instructions on how to get API keys for different providers.
- **Provider Selection:** A combo box to select the default provider (OpenAI, Anthropic, Ollama).
- **Dynamic Model Fetching:** Selecting a provider triggers a live API call to fetch available models.
- **UI:** The Model selection dropdown is widened (3x) for legibility.
- **Test Connection:** A button to verify API connectivity and keys immediately.
- **Ollama (Local):**
    - Detects if Ollama is installed.
    - **Auto-Pull:** If a selected local model is not installed, clicking "Next" triggers a background download with a real-time progress bar.

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

#### Page 4: Completion
- **Content:** Summary of actions.
- **Post-Wizard Action:** Automatically opens the project `README.md` from the `research/` folder to guide the user.

### 2. `MainWindow` / Application Startup Integration
- Modified startup logic in `MainWindow` constructor.
- Checks `QSettings` for `firstRunComplete`.
- If it's the first run, instantiates and executes `OnboardingWizard` modally before restoring session.

### 3. Project Initialization Logic
- Refactored `ProjectManager` to handle default project template creation.
- Logic can be called seamlessly by the Onboarding Wizard upon completion or via manual "New Project" actions.

## Next Steps (Completed)
- Core onboarding and Scrivener/Word data import (Phase 12) are now fully integrated.

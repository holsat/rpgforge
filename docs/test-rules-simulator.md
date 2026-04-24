# Rules Simulator — End-to-End Test Script

A step-by-step walkthrough for someone who has never used the Rules
Simulator before. Follow each section in order; each has an explicit
"Expected" checklist so you can catch a break before moving on.

If something in an **Expected** block does not match what you see, stop
and note the exact step — that's the regression to file.

---

## 0. Prerequisites

- A build of the app:
  `cmake --build build -j$(nproc)` (or run via `./scripts/run-nvidia.sh`
  if the AMD-GPU preview crash is still open on your machine).
- **An LLM provider configured.** At minimum one provider with a valid
  API key and a default chat model. Settings → LLM tab. Most of the
  simulator's quality rides on a mid/top-tier model (Gemini Pro, Claude
  Sonnet, GPT-4-class); a tiny local model will produce weak turns.
- **An RPG project opened** (File → Open, or create a fresh one with
  File → New Project). The simulator refuses to start without a project.
- **At least one Markdown character file** somewhere in the project —
  a LoreKeeper `Characters/` dossier works perfectly, or any `.md` with
  a name and a few lines of description. Minimum 1, two is more
  interesting.
- *(Optional)* A Markdown scenario file describing the scene.

---

## 1. Open the Simulator panel

**Do:**
1. Look at the left sidebar of the main window.
2. Click the ▶ (`media-playback-start-symbolic`) icon labelled
   **Rule Simulation**.

**Expected:**
- The left sidebar flips from the Project Tree view to a Simulation
  view with three tabs at the top: **Live Log**, **Participants**,
  **World State**.
- Above the tabs you see a toolbar: **Start** button (blue-highlighted),
  **Stop** button (disabled), **Save** (disk icon, also disabled), a
  **Scenario** field with a folder picker, a **Max Turns** spinner
  (default 20), a **Batch** checkbox with a batch-count spinner
  (default 10, disabled unless Batch is checked), and an **Aggression**
  slider (default around the middle).

---

## 2. Add participants

**Do:**
1. Click the **Participants** tab.
2. Click **Add…** and select a character `.md` file (a LoreKeeper
   Character dossier works; also accepts `.json` or `.yaml`).
3. Repeat to add a second character.
4. *(Alternate)* Drag a character file from the Project Tree directly
   onto the Participants list — should also accept it.

**Expected:**
- Each added character appears as a row showing its filename (or
  `name` field from a JSON file).
- A **Remove** button next to each row lets you drop a participant.
- If you close and reopen the panel within the same session,
  participants persist until the app is restarted (they are not
  saved to disk).

**Gotcha to verify:** clicking **Start** with zero participants should
be a silent no-op (no crash, no partially-started run). Test this
once by emptying the list and pressing Start.

---

## 3. *(Optional)* Load a scenario

**Do:**
1. In the toolbar, click the folder icon next to **Scenario**.
2. Pick a Markdown file describing the scene (e.g. a short setup
   paragraph: "The PCs stand at a tavern door. A cloaked figure
   blocks the path.").

**Expected:**
- The Scenario field shows the file's path.
- If you skip this step, the simulator uses the built-in default:
  *"A standard combat encounter."*

---

## 4. Configure turn count and aggression

**Do:**
1. Leave **Max Turns** at `20` for the first run. Short enough that you
   can read the whole log; long enough for interesting behaviour.
2. Drag the **Aggression** slider to step 3 (balanced) for the
   baseline, or try the extremes later.
3. Leave **Batch** unchecked for the first run.

**Expected values and meaning:**
- 1 — Narrative/casual actors, lean away from mechanical exploits.
- 3 — Balanced.
- 5 — Munchkin: actors aggressively min/max within the rules.

---

## 5. Verify Simulation agents have providers set

**Do:**
1. Open Settings → AI Agents tab.
2. Confirm rows for **Simulation Arbiter**, **Simulation Griot**, and
   **Simulation Actor** each have a provider + model selected.
3. If any show a blank model combo, paste the API key on the LLM tab
   for that provider and the combos will auto-populate.

**Expected:**
- All three agent rows show a model (e.g. `gemini-2.5-flash`,
  `claude-sonnet-4-5`, `gpt-4o`).
- If any are blank, the simulator will silently fall back to the
  global default provider (`llm/provider` — the top of your drag list
  in the LLM tab); note this for the log.

---

## 6. First solo run (single turn read-through)

**Do:**
1. Back on the Simulation panel, click **Start**.
2. Watch the **Live Log** tab.

**Expected stream (one full turn):**
1. A dark-coloured system line: `--- Turn 1: <Actor Name> ---`.
2. An actor-decision line: `<Actor Name> decides to: …` (the AI's
   proposed action in first-ish person).
3. An Arbiter line: `Arbiter: …` describing rule lookups, any dice
   rolls, and patches to world state.
4. A Griot line: `Griot: …` narrating the outcome in prose.
5. After ~1 second, another `--- Turn 2: … ---` marker begins the
   next actor's turn.

**Also check:**
- Click the **World State** tab mid-run — it should render a tree of
  key/value pairs being mutated in real time (HP, position, inventory,
  whatever the Arbiter is tracking for that scenario).
- The **Stop** button is now enabled.

**Failure modes to log:**
- No turns advance after Start. → Probably a provider auth failure;
  check `~/.local/share/rpgforge/rpgforge_debug.log` for the real
  error (our recent changes now surface these properly).
- One actor speaks but the other never gets a turn. → Check the
  Participants order and the log for "thinking…" stalls.
- Arbiter never appears, only Actor + Griot. → Check
  `simulation/sim_arbiter_provider` has a model set.

---

## 7. Stop the run

**Do:**
1. Click **Stop** at any point during the run.

**Expected:**
- The log emits a `--- Simulation stopped by user ---` (or similar
  terminator) within one turn.
- **Start** becomes enabled again.
- Final world state sticks in the World State tab for inspection.

---

## 8. Save the run

**Do:**
1. Click the disk icon in the toolbar.

**Expected:**
- A file appears at
  `<project_path>/simulations/sim_YYYYMMDD_HHmmss.json`.
- Contents: a JSON snapshot of the final world state. (Live-log
  transcript is NOT in this file; it's view-only for now.)
- If the project path isn't set, save is a silent no-op — confirm
  this doesn't crash.

---

## 9. Batch mode (regression cover)

**Do:**
1. Check **Batch**.
2. Set batch count to `5` and max turns to `10`.
3. Click **Start**.

**Expected:**
- The Live Log shows only the Actor-decides lines (Arbiter + Griot
  narrative are suppressed for batch speed) and per-run summaries.
- After all 5 runs complete, the log prints:
  ```
  === BATCH ANALYTICS ===
  Runs: 5
  Average duration: <N.N> turns
  ```
- **Save** now stores a JSON file with an array of 5 final states,
  not just one.

---

## 10. Aggression sweep (sanity check)

**Do:**
1. Run one short sim (max 10 turns) with aggression at **1**.
2. Run the same setup with aggression at **5**.
3. Compare the two Live Logs.

**Expected:**
- Level 1 runs lean narrative ("I try to reason with the guard…").
- Level 5 runs lean mechanical exploit ("I flank for advantage and
  burn my level-3 spell slot for max damage").
- Both should still obey Arbiter rulings and not corrupt world state.

---

## 11. Exit and resume check

**Do:**
1. Close the Simulation panel (click a different sidebar icon).
2. Re-open it.

**Expected:**
- Participants list is preserved for the session.
- Live Log is cleared (by design — each Start is a fresh run).
- Toolbar state is reset: Start enabled, Stop disabled.

**Gotcha:** the Participants list does **not** persist across app
restart. If you start the app fresh, you'll need to re-add them.

---

## Quick regression checklist (for future testing)

Copy/paste into your test tracker:

- [ ] Opens via left-sidebar ▶ icon.
- [ ] Three tabs: Live Log, Participants, World State.
- [ ] Drag/drop onto Participants tab adds actors.
- [ ] Start with zero actors is a no-op.
- [ ] Turns auto-advance, one per ~1s.
- [ ] Actor / Arbiter / Griot lines all appear in non-batch mode.
- [ ] World State tab updates live.
- [ ] Stop halts within one turn.
- [ ] Save writes `simulations/sim_<timestamp>.json`.
- [ ] Batch mode prints analytics block at end.
- [ ] Aggression slider visibly shifts actor behaviour.
- [ ] No crash when switching sidebar panels mid-run.

---

## Appendix — where each setting lives

| Setting | Location |
|---|---|
| Provider/model for Actor / Arbiter / Griot | Settings → AI Agents tab |
| Prompts for each simulation agent | Settings → Prompts tab |
| Global fallback provider order | Settings → LLM tab (drag to reorder) |
| Scenario file | Simulation panel toolbar, folder picker |
| Participants | Simulation panel → Participants tab → Add |
| Output JSON | `<project>/simulations/sim_*.json` |
| Debug logs | `~/.local/share/rpgforge/rpgforge_debug.log` |

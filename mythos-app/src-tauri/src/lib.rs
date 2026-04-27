// Mythos — Tauri entry point and project bridge.

use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;

#[derive(Serialize, Deserialize)]
pub struct AppInfo {
    pub name: String,
    pub version: String,
}

#[derive(Serialize, Deserialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct MythosVariable {
    pub id: String,
    pub token: String,
    pub label: String,
    pub kind: String,
}

#[derive(Serialize, Deserialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct MythosEntity {
    pub id: String,
    pub name: String,
    pub role: String,
    pub glyph: Option<String>,
}

#[derive(Serialize, Deserialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct MythosDocument {
    pub id: String,
    pub title: String,
    pub path: String,
    pub body: String,
    pub word_count: usize,
}

#[derive(Serialize, Deserialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct MythosProject {
    pub schema_version: u32,
    pub name: String,
    pub active_document_id: String,
    pub documents: Vec<MythosDocument>,
    pub variables: Vec<MythosVariable>,
    pub characters: Vec<MythosEntity>,
    pub locations: Vec<MythosEntity>,
}

#[tauri::command]
fn app_info() -> AppInfo {
    AppInfo {
        name: "Mythos".into(),
        version: env!("CARGO_PKG_VERSION").into(),
    }
}

/// Echo command — useful sanity-check from the frontend during early dev.
/// Call from JS:
///   import { invoke } from '@tauri-apps/api/core';
///   const reply = await invoke('greet', { name: 'Elara' });
#[tauri::command]
fn greet(name: &str) -> String {
    format!("Welcome, {}. The Ledger remembers.", name)
}

#[tauri::command]
fn sample_project() -> MythosProject {
    default_project()
}

#[tauri::command]
fn load_project(path: String) -> Result<MythosProject, String> {
    let path = PathBuf::from(path);
    let raw = fs::read_to_string(&path)
        .map_err(|err| format!("Could not read {}: {}", path.display(), err))?;
    serde_json::from_str(&raw)
        .map_err(|err| format!("Could not parse {}: {}", path.display(), err))
}

#[tauri::command]
fn save_project(path: String, project: MythosProject) -> Result<(), String> {
    let path = PathBuf::from(path);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .map_err(|err| format!("Could not create {}: {}", parent.display(), err))?;
    }

    let raw = serde_json::to_string_pretty(&project)
        .map_err(|err| format!("Could not serialize project: {}", err))?;
    fs::write(&path, raw)
        .map_err(|err| format!("Could not write {}: {}", path.display(), err))
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_store::Builder::default().build())
        .invoke_handler(tauri::generate_handler![
            app_info,
            greet,
            sample_project,
            load_project,
            save_project
        ])
        .run(tauri::generate_context!())
        .expect("error while running Mythos");
}

fn default_project() -> MythosProject {
    let body = "The cobblestones of Oakhaven were slick with evening rain. [@ElaraVance] pulled her cloak tighter, her eyes scanning the shadowy market stalls. Her goal lay beyond, in the mysterious [@HighpassInn]. The air was thick with tension; the whispers of the looming [@DuskWar] hung heavy upon every threshold.\n\nAt the end of the alley, where a single lantern threw its small amber circle against the wet stone, she paused. The cursor of memory hovered over a name she had not spoken aloud since Aethel: [@Highpass\n\nShe drew breath and stepped past the threshold. Inside, the firelight made an island of warmth amid the chill. Brom the innkeep looked up, his eye catching the silver clasp at her throat — the same clasp her mother had pressed into her hand the night the [@DuskWar] began.\n\nHeading Stalls. She ordered ale she would not drink and watched the door. The [@TheShard] was here, somewhere — bound, she suspected, to the corner table where a hooded figure sat with their back to the room. She had crossed three kingdoms for this moment. She would not waste it on hesitation.";

    MythosProject {
        schema_version: 1,
        name: "The Sunstone Cycle".into(),
        active_document_id: "chapter-3".into(),
        documents: vec![MythosDocument {
            id: "chapter-3".into(),
            title: "Chapter III · The Labyrinth".into(),
            path: "Drafts/Chapter 3.md".into(),
            body: body.into(),
            word_count: word_count(body),
        }],
        variables: vec![
            variable("elara", "[@ElaraVance]", "Elara Vance", "Character"),
            variable("highpass", "[@HighpassInn]", "Highpass Inn", "Location"),
            variable("aethelgard", "[@Aethelgard]", "Aethelgard", "Location"),
            variable("dusk-war", "[@DuskWar]", "Dusk War", "Concept"),
            variable("shard", "[@TheShard]", "The Shard", "Artifact"),
            variable("kaelen", "[@Kaelen]", "Kaelen", "Character"),
        ],
        characters: vec![
            entity("lirael", "Lirael", "Elven Warrior", Some("LI")),
            entity("kaelen", "Kaelen", "Shadowblade", Some("KA")),
            entity("aria", "Aria", "Mage", Some("AR")),
            entity("brom", "Brom", "Warrior", Some("BR")),
        ],
        locations: vec![
            entity("aethelgard", "Aethelgard", "Capital City", None),
            entity("highpass", "Highpass Inn", "Crossroads", None),
            entity("shadowvale", "Shadowvale", "Hidden Valley", None),
        ],
    }
}

fn variable(id: &str, token: &str, label: &str, kind: &str) -> MythosVariable {
    MythosVariable {
        id: id.into(),
        token: token.into(),
        label: label.into(),
        kind: kind.into(),
    }
}

fn entity(id: &str, name: &str, role: &str, glyph: Option<&str>) -> MythosEntity {
    MythosEntity {
        id: id.into(),
        name: name.into(),
        role: role.into(),
        glyph: glyph.map(str::to_string),
    }
}

fn word_count(text: &str) -> usize {
    text.split_whitespace().count()
}

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
    #[serde(default)]
    pub id: String,
    #[serde(default)]
    pub token: String,
    #[serde(default)]
    pub label: String,
    #[serde(default)]
    pub kind: String,
}

#[derive(Serialize, Deserialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct MythosEntity {
    #[serde(default)]
    pub id: String,
    #[serde(default)]
    pub name: String,
    #[serde(default)]
    pub role: String,
    #[serde(default)]
    pub glyph: Option<String>,
}

#[derive(Serialize, Deserialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct MythosDocument {
    #[serde(default)]
    pub id: String,
    #[serde(default)]
    pub title: String,
    #[serde(default)]
    pub path: String,
    #[serde(default)]
    pub body: String,
    #[serde(default)]
    pub word_count: usize,
}

#[derive(Serialize, Deserialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct MythosMetadata {
    #[serde(default = "now_iso")]
    pub created_at: String,
    #[serde(default = "now_iso")]
    pub updated_at: String,
    #[serde(default = "app_version")]
    pub app_version: String,
}

#[derive(Serialize, Deserialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct MythosProject {
    #[serde(default = "schema_version")]
    pub schema_version: u32,
    #[serde(default)]
    pub name: String,
    #[serde(default = "default_metadata")]
    pub metadata: MythosMetadata,
    #[serde(default)]
    pub active_document_id: String,
    #[serde(default)]
    pub documents: Vec<MythosDocument>,
    #[serde(default)]
    pub variables: Vec<MythosVariable>,
    #[serde(default)]
    pub characters: Vec<MythosEntity>,
    #[serde(default)]
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
    let project = serde_json::from_str(&raw)
        .map_err(|err| format!("Could not parse {}: {}", path.display(), err))?;
    normalize_project(project, false)
}

#[tauri::command]
fn save_project(path: String, project: MythosProject) -> Result<(), String> {
    let path = PathBuf::from(path);
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .map_err(|err| format!("Could not create {}: {}", parent.display(), err))?;
    }

    let project = normalize_project(project, true)?;
    let raw = serde_json::to_string_pretty(&project)
        .map_err(|err| format!("Could not serialize project: {}", err))?;
    fs::write(&path, raw).map_err(|err| format!("Could not write {}: {}", path.display(), err))
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
        metadata: default_metadata(),
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

fn normalize_project(
    mut project: MythosProject,
    touch_updated_at: bool,
) -> Result<MythosProject, String> {
    if project.schema_version != 1 {
        return Err(format!(
            "Unsupported Mythos schema version {}",
            project.schema_version
        ));
    }

    if project.name.trim().is_empty() {
        project.name = "Untitled Mythos Project".into();
    }

    for (index, document) in project.documents.iter_mut().enumerate() {
        if document.id.trim().is_empty() {
            document.id = format!("document-{}", index + 1);
        }
        if document.title.trim().is_empty() {
            document.title = format!("Untitled {}", index + 1);
        }
        if document.path.trim().is_empty() {
            document.path = format!("Drafts/{}.md", document.title);
        }
        document.word_count = word_count(&document.body);
    }

    if !project
        .documents
        .iter()
        .any(|document| document.id == project.active_document_id)
    {
        project.active_document_id = project
            .documents
            .first()
            .map(|document| document.id.clone())
            .unwrap_or_default();
    }

    if touch_updated_at {
        project.metadata.updated_at = now_iso();
    }
    Ok(project)
}

fn schema_version() -> u32 {
    1
}

fn app_version() -> String {
    env!("CARGO_PKG_VERSION").into()
}

fn default_metadata() -> MythosMetadata {
    let timestamp = now_iso();
    MythosMetadata {
        created_at: timestamp.clone(),
        updated_at: timestamp,
        app_version: app_version(),
    }
}

fn now_iso() -> String {
    chrono::Utc::now().to_rfc3339()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn normalizes_missing_project_fields() {
        let project = MythosProject {
            schema_version: 1,
            name: "".into(),
            metadata: default_metadata(),
            active_document_id: "missing".into(),
            documents: vec![MythosDocument {
                id: "".into(),
                title: "".into(),
                path: "".into(),
                body: "One two three".into(),
                word_count: 0,
            }],
            variables: vec![],
            characters: vec![],
            locations: vec![],
        };

        let normalized = normalize_project(project, false).expect("project should normalize");

        assert_eq!(normalized.name, "Untitled Mythos Project");
        assert_eq!(normalized.active_document_id, "document-1");
        assert_eq!(normalized.documents[0].title, "Untitled 1");
        assert_eq!(normalized.documents[0].word_count, 3);
    }

    #[test]
    fn rejects_unsupported_schema_versions() {
        let project = MythosProject {
            schema_version: 99,
            name: "Future Project".into(),
            metadata: default_metadata(),
            active_document_id: "".into(),
            documents: vec![],
            variables: vec![],
            characters: vec![],
            locations: vec![],
        };

        let error = match normalize_project(project, false) {
            Ok(_) => panic!("schema should be rejected"),
            Err(error) => error,
        };

        assert!(error.contains("Unsupported Mythos schema version 99"));
    }
}

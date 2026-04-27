// Mythos — Tauri entry point.
//
// This is a minimal main; most app surface is in the React frontend.
// As you build out file I/O, AI calls, and project storage, expose them
// as #[tauri::command] handlers below and register them in invoke_handler.

use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize)]
pub struct AppInfo {
    pub name: String,
    pub version: String,
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

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_store::Builder::default().build())
        .invoke_handler(tauri::generate_handler![app_info, greet])
        .run(tauri::generate_context!())
        .expect("error while running Mythos");
}

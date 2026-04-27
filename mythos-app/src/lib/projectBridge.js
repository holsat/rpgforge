import { invoke } from '@tauri-apps/api/core';

export const FALLBACK_PROJECT = {
  schemaVersion: 1,
  name: 'The Sunstone Cycle',
  activeDocumentId: 'chapter-3',
  documents: [
    {
      id: 'chapter-3',
      title: 'Chapter III · The Labyrinth',
      path: 'Drafts/Chapter 3.md',
      wordCount: 167,
      body: [
        'The cobblestones of Oakhaven were slick with evening rain. [@ElaraVance] pulled her cloak tighter, her eyes scanning the shadowy market stalls. Her goal lay beyond, in the mysterious [@HighpassInn]. The air was thick with tension; the whispers of the looming [@DuskWar] hung heavy upon every threshold.',
        'At the end of the alley, where a single lantern threw its small amber circle against the wet stone, she paused. The cursor of memory hovered over a name she had not spoken aloud since Aethel: [@Highpass',
        'She drew breath and stepped past the threshold. Inside, the firelight made an island of warmth amid the chill. Brom the innkeep looked up, his eye catching the silver clasp at her throat — the same clasp her mother had pressed into her hand the night the [@DuskWar] began.',
        'Heading Stalls. She ordered ale she would not drink and watched the door. The [@TheShard] was here, somewhere — bound, she suspected, to the corner table where a hooded figure sat with their back to the room. She had crossed three kingdoms for this moment. She would not waste it on hesitation.',
      ].join('\n\n'),
    },
  ],
  variables: [
    { id: 'elara', token: '[@ElaraVance]', label: 'Elara Vance', kind: 'Character' },
    { id: 'highpass', token: '[@HighpassInn]', label: 'Highpass Inn', kind: 'Location' },
    { id: 'aethelgard', token: '[@Aethelgard]', label: 'Aethelgard', kind: 'Location' },
    { id: 'dusk-war', token: '[@DuskWar]', label: 'Dusk War', kind: 'Concept' },
    { id: 'shard', token: '[@TheShard]', label: 'The Shard', kind: 'Artifact' },
    { id: 'kaelen', token: '[@Kaelen]', label: 'Kaelen', kind: 'Character' },
  ],
  characters: [
    { id: 'lirael', name: 'Lirael', role: 'Elven Warrior', glyph: 'LI' },
    { id: 'kaelen', name: 'Kaelen', role: 'Shadowblade', glyph: 'KA' },
    { id: 'aria', name: 'Aria', role: 'Mage', glyph: 'AR' },
    { id: 'brom', name: 'Brom', role: 'Warrior', glyph: 'BR' },
  ],
  locations: [
    { id: 'aethelgard', name: 'Aethelgard', role: 'Capital City' },
    { id: 'highpass', name: 'Highpass Inn', role: 'Crossroads' },
    { id: 'shadowvale', name: 'Shadowvale', role: 'Hidden Valley' },
  ],
};

export async function getSampleProject() {
  try {
    return await invoke('sample_project');
  } catch {
    return FALLBACK_PROJECT;
  }
}

export async function loadProject(path) {
  return invoke('load_project', { path });
}

export async function saveProject(path, project) {
  return invoke('save_project', { path, project });
}

export function activeDocument(project) {
  return project?.documents?.find(doc => doc.id === project.activeDocumentId)
    || project?.documents?.[0]
    || null;
}

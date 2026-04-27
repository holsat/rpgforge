import { invoke } from '@tauri-apps/api/core';
import { open, save } from '@tauri-apps/plugin-dialog';

export const MYTHOS_SCHEMA_VERSION = 1;

const MYTHOS_FILE_FILTER = {
  name: 'Mythos Project',
  extensions: ['mythos'],
};

function wordCount(text = '') {
  return text.trim() ? text.trim().split(/\s+/).length : 0;
}

function slugify(value) {
  return value
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '') || 'untitled';
}

function nowIso() {
  return new Date().toISOString();
}

export const FALLBACK_PROJECT = {
  schemaVersion: MYTHOS_SCHEMA_VERSION,
  name: 'The Sunstone Cycle',
  metadata: {
    createdAt: '2026-04-27T00:00:00.000Z',
    updatedAt: '2026-04-27T00:00:00.000Z',
    appVersion: '0.1.0',
  },
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
        'She drew breath and stepped past the threshold. Inside, the firelight made an island of warmth amid the chill. Brom the innkeep looked up, his eye catching the silver clasp at her throat - the same clasp her mother had pressed into her hand the night the [@DuskWar] began.',
        'Heading Stalls. She ordered ale she would not drink and watched the door. The [@TheShard] was here, somewhere - bound, she suspected, to the corner table where a hooded figure sat with their back to the room. She had crossed three kingdoms for this moment. She would not waste it on hesitation.',
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

export function normalizeProject(project) {
  const source = project || {};
  const documents = Array.isArray(source.documents) ? source.documents : [];
  const normalizedDocuments = documents.map((document, index) => {
    const body = document.body || '';
    const title = document.title || `Untitled ${index + 1}`;
    return {
      id: document.id || `document-${index + 1}`,
      title,
      path: document.path || `Drafts/${title}.md`,
      body,
      contentHtml: document.contentHtml || '',
      wordCount: Number.isFinite(document.wordCount) ? document.wordCount : wordCount(body),
    };
  });
  const activeDocument = normalizedDocuments.find(doc => doc.id === source.activeDocumentId)
    || normalizedDocuments[0]
    || null;

  return {
    schemaVersion: source.schemaVersion || MYTHOS_SCHEMA_VERSION,
    name: source.name || 'Untitled Mythos Project',
    metadata: {
      createdAt: source.metadata?.createdAt || nowIso(),
      updatedAt: source.metadata?.updatedAt || nowIso(),
      appVersion: source.metadata?.appVersion || '0.1.0',
    },
    activeDocumentId: activeDocument?.id || '',
    documents: normalizedDocuments,
    variables: Array.isArray(source.variables) ? source.variables : [],
    characters: Array.isArray(source.characters) ? source.characters : [],
    locations: Array.isArray(source.locations) ? source.locations : [],
  };
}

export function createBlankProject(name = 'Untitled Mythos Project') {
  const createdAt = nowIso();
  return normalizeProject({
    schemaVersion: MYTHOS_SCHEMA_VERSION,
    name,
    metadata: {
      createdAt,
      updatedAt: createdAt,
      appVersion: '0.1.0',
    },
    activeDocumentId: 'opening-scene',
    documents: [
      {
        id: 'opening-scene',
        title: 'Opening Scene',
        path: 'Drafts/Opening Scene.md',
        body: '',
        contentHtml: '',
        wordCount: 0,
      },
    ],
    variables: [],
    characters: [],
    locations: [],
  });
}

export async function getSampleProject() {
  try {
    return normalizeProject(await invoke('sample_project'));
  } catch {
    return normalizeProject(FALLBACK_PROJECT);
  }
}

export async function loadProject(path) {
  return normalizeProject(await invoke('load_project', { path }));
}

export async function saveProject(path, project) {
  return invoke('save_project', {
    path,
    project: normalizeProject({
      ...project,
      metadata: {
        ...project?.metadata,
        updatedAt: nowIso(),
      },
    }),
  });
}

export async function chooseProjectToOpen() {
  return open({
    multiple: false,
    directory: false,
    filters: [MYTHOS_FILE_FILTER],
  });
}

export async function chooseProjectSavePath(projectName) {
  return save({
    defaultPath: `${slugify(projectName || 'untitled')}.mythos`,
    filters: [MYTHOS_FILE_FILTER],
  });
}

export function activeDocument(project) {
  return project?.documents?.find(doc => doc.id === project.activeDocumentId)
    || project?.documents?.[0]
    || null;
}

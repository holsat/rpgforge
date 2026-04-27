import React from 'react';
import { Icon, Compass } from './components/icons.jsx';
import { useTweaks, TweaksPanel, TweakSection, TweakText, TweakRadio, TweakSlider, TweakButton } from './components/tweaks-panel.jsx';
import { EditorScreen } from './screens/EditorScreen.jsx';
import { CodexScreen } from './screens/CodexScreen.jsx';
import { CorkboardScreen } from './screens/CorkboardScreen.jsx';
import { TimelineScreen } from './screens/TimelineScreen.jsx';
import { AtlasScreen } from './screens/AtlasScreen.jsx';
import { WeaverScreen } from './screens/WeaverScreen.jsx';
import { SettingsScreen } from './screens/SettingsScreen.jsx';
import { FocusMode } from './screens/FocusMode.jsx';
import {
  chooseProjectSavePath,
  chooseProjectToOpen,
  createBlankProject,
  getSampleProject,
  loadProject,
  saveProject,
} from './lib/projectBridge.js';

// Mythos — App shell, rail, route state, focus mode toggle

const ROUTES = [
  { id: 'editor',    icon: 'feather',   label: 'Draft' },
  { id: 'codex',     icon: 'book',      label: 'Codex' },
  { id: 'corkboard', icon: 'corkboard', label: 'Corkboard' },
  { id: 'timeline',  icon: 'timeline',  label: 'Timeline' },
  { id: 'atlas',     icon: 'map',       label: 'Atlas' },
  { id: 'weaver',    icon: 'graph',     label: 'Weaver' },
];

function Rail({ route, setRoute, onFocus }) {
  return (
    <nav className="rail">
      <div className="rail-logo"><Compass size={28}/></div>
      <div className="rail-items">
        {ROUTES.map(r => (
          <div
            key={r.id}
            className={'rail-item' + (route === r.id ? ' active' : '')}
            onClick={() => setRoute(r.id)}
          >
            <Icon name={r.icon} size={20}/>
            <span className="tip">{r.label}</span>
          </div>
        ))}
      </div>
      <div className="rail-bottom">
        <div className="rail-item" onClick={onFocus} title="Focus Mode">
          <Icon name="sparkle" size={20}/>
          <span className="tip">Focus Mode</span>
        </div>
        <div
          className={'rail-item' + (route === 'settings' ? ' active' : '')}
          onClick={() => setRoute('settings')}
        >
          <Icon name="settings" size={20}/>
          <span className="tip">Ledger</span>
        </div>
        <div className="rail-item">
          <Icon name="help" size={20}/>
          <span className="tip">Help</span>
        </div>
      </div>
    </nav>
  );
}

function ProjectActionButton({ children, onClick, disabled }) {
  return (
    <button className="titlebar-action" onClick={onClick} disabled={disabled}>
      {children}
    </button>
  );
}

function TitleBar({
  project,
  projectPath,
  dirty,
  status,
  onNewProject,
  onOpenProject,
  onSaveProject,
  onSaveProjectAs,
}) {
  return (
    <div className="titlebar">
      <div className="traffic">
        <span className="light red"/>
        <span className="light yellow"/>
        <span className="light green"/>
      </div>
      <div className="titlebar-title">
        <Compass size={14}/>
        <span>Mythos</span>
      </div>
      <div className="titlebar-right">
        <span className="project-status">{status}</span>
        <div className="project-actions">
          <ProjectActionButton onClick={onNewProject}>New</ProjectActionButton>
          <ProjectActionButton onClick={onOpenProject}>Open</ProjectActionButton>
          <ProjectActionButton onClick={onSaveProject} disabled={!dirty && Boolean(projectPath)}>Save</ProjectActionButton>
          <ProjectActionButton onClick={onSaveProjectAs}>Save As</ProjectActionButton>
        </div>
        <div className="project">
          <Icon name="book" size={12}/>
          <span>{project}{dirty ? ' *' : ''}</span>
          <Icon name="chevron-down" size={12}/>
        </div>
      </div>
    </div>
  );
}

// Tweak defaults — the host (Mythos Studio editor) rewrites the JSON
// between the markers when the user changes values in the Tweaks panel.
const TWEAK_DEFAULTS = /*EDITMODE-BEGIN*/{
  "projectName": "The Sunstone Cycle",
  "tokenStyle": "pill",
  "editorFontSize": 19,
  "editorLineHeight": 1.7,
  "goldIntensity": "signature"
}/*EDITMODE-END*/;

export function App() {
  const [t, setTweak] = useTweaks(TWEAK_DEFAULTS);
  const [projectData, setProjectData] = React.useState(null);
  const [projectPath, setProjectPath] = React.useState('');
  const [dirty, setDirty] = React.useState(false);
  const [projectStatus, setProjectStatus] = React.useState('Loading sample project');
  const [route, setRoute] = React.useState(() => {
    try { return localStorage.getItem('mythos-route') || 'editor'; } catch { return 'editor'; }
  });
  const [focusMode, setFocusMode] = React.useState(false);

  React.useEffect(() => {
    try { localStorage.setItem('mythos-route', route); } catch {}
  }, [route]);

  React.useEffect(() => {
    let cancelled = false;
    getSampleProject().then(project => {
      if (!cancelled) {
        setProjectData(project);
        setProjectStatus('Sample project');
      }
    }).catch(error => {
      if (!cancelled) setProjectStatus(error.message || 'Could not load sample');
    });
    return () => { cancelled = true; };
  }, []);

  const setLoadedProject = React.useCallback((project, path = '', isDirty = false) => {
    setProjectData(project);
    setProjectPath(path);
    setDirty(isDirty);
  }, []);

  const handleNewProject = React.useCallback(() => {
    setLoadedProject(createBlankProject('Untitled Mythos Project'), '', true);
    setProjectStatus('New unsaved project');
    setRoute('editor');
  }, [setLoadedProject]);

  const handleOpenProject = React.useCallback(async () => {
    try {
      const selected = await chooseProjectToOpen();
      if (!selected) return;
      const path = Array.isArray(selected) ? selected[0] : selected;
      const project = await loadProject(path);
      setLoadedProject(project, path);
      setProjectStatus('Opened project');
      setRoute('editor');
    } catch (error) {
      setProjectStatus(error.message || 'Open failed');
    }
  }, [setLoadedProject]);

  const handleSaveProjectAs = React.useCallback(async () => {
    if (!projectData) return;
    try {
      const path = await chooseProjectSavePath(projectData.name);
      if (!path) return;
      await saveProject(path, projectData);
      setProjectPath(path);
      setDirty(false);
      setProjectStatus('Saved project');
    } catch (error) {
      setProjectStatus(error.message || 'Save failed');
    }
  }, [projectData]);

  const handleSaveProject = React.useCallback(async () => {
    if (!projectData) return;
    if (!projectPath) {
      await handleSaveProjectAs();
      return;
    }
    try {
      await saveProject(projectPath, projectData);
      setDirty(false);
      setProjectStatus('Saved project');
    } catch (error) {
      setProjectStatus(error.message || 'Save failed');
    }
  }, [handleSaveProjectAs, projectData, projectPath]);

  const handleDocumentChange = React.useCallback((documentId, patch) => {
    setProjectData(project => {
      if (!project) return project;
      return {
        ...project,
        documents: project.documents.map(document => (
          document.id === documentId ? { ...document, ...patch } : document
        )),
      };
    });
    setDirty(true);
    setProjectStatus('Unsaved changes');
  }, []);

  React.useEffect(() => {
    const onKey = (e) => {
      if ((e.metaKey || e.ctrlKey) && e.shiftKey && e.key.toLowerCase() === 'f') {
        e.preventDefault();
        setFocusMode(f => !f);
      }
      if (e.key === 'Escape' && focusMode) setFocusMode(false);
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [focusMode]);

  // Apply tweaks to CSS vars
  React.useEffect(() => {
    const r = document.documentElement;
    const intensity = { subtle: '0.6', signature: '1', strong: '1.4' }[t.goldIntensity] || '1';
    r.style.setProperty('--gold-pulse-mul', intensity);
    r.style.setProperty('--ms-font-size', t.editorFontSize + 'px');
    r.style.setProperty('--ms-line-height', t.editorLineHeight);
  }, [t]);

  const projectName = projectData?.name || t.projectName;
  const screens = {
    editor:    <EditorScreen tokenStyle={t.tokenStyle} project={projectName} projectData={projectData} onDocumentChange={handleDocumentChange}/>,
    codex:     <CodexScreen projectData={projectData}/>,
    corkboard: <CorkboardScreen project={projectName}/>,
    timeline:  <TimelineScreen/>,
    atlas:     <AtlasScreen/>,
    weaver:    <WeaverScreen/>,
    settings:  <SettingsScreen/>,
  };

  return (
    <div className="app-shell" data-screen-label={`Mythos · ${route}`}>
      <TitleBar
        project={projectName}
        projectPath={projectPath}
        dirty={dirty}
        status={projectStatus}
        onNewProject={handleNewProject}
        onOpenProject={handleOpenProject}
        onSaveProject={handleSaveProject}
        onSaveProjectAs={handleSaveProjectAs}
      />
      <div className="app-body">
        <Rail route={route} setRoute={setRoute} onFocus={() => setFocusMode(true)}/>
        <main className="screen">{screens[route]}</main>
      </div>
      {focusMode && <FocusMode onLeave={() => setFocusMode(false)}/>}

      <TweaksPanel title="Tweaks">
        <TweakSection label="Project"/>
        <TweakText label="Sample project name" value={t.projectName}
                   onChange={v => setTweak('projectName', v)}/>

        <TweakSection label="Variable token style"/>
        <TweakRadio label="Style" value={t.tokenStyle}
                    options={['pill','bracket','inline']}
                    onChange={v => setTweak('tokenStyle', v)}/>

        <TweakSection label="Editor"/>
        <TweakSlider label="Font size" value={t.editorFontSize}
                     min={15} max={24} unit="px"
                     onChange={v => setTweak('editorFontSize', v)}/>
        <TweakSlider label="Line height" value={t.editorLineHeight}
                     min={1.4} max={2.0} step={0.05}
                     onChange={v => setTweak('editorLineHeight', v)}/>

        <TweakSection label="Atmosphere"/>
        <TweakRadio label="Gold intensity" value={t.goldIntensity}
                    options={['subtle','signature','strong']}
                    onChange={v => setTweak('goldIntensity', v)}/>

        <TweakSection label="Modes"/>
        <TweakButton label="Enter Focus Mode (⌘⇧F)"
                     onClick={() => setFocusMode(true)}/>
      </TweaksPanel>
    </div>
  );
}

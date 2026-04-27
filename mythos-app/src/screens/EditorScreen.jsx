import React from 'react';
import { Icon, Ember } from '../components/icons.jsx';
import { MythosEditor } from '../components/MythosEditor.jsx';
import { activeDocument, FALLBACK_PROJECT } from '../lib/projectBridge.js';

// Mythos — Editor screen (Draft view)
// Manuscript editor + inline AI menu + Muse sidebar

const FALLBACK_VARIABLES = FALLBACK_PROJECT.variables;

function InlineAIMenu({ onClose }) {
  // Use spans (not buttons / divs) since this renders inside a <p>.
  return (
    <span className="inline-ai" style={{ left: '40%', top: '8px' }}>
      <span className="iai-btn" onClick={onClose}><Icon name="sparkle" size={14} className="ico"/>AI Enhance</span>
      <span className="iai-btn" onClick={onClose}><Icon name="graph" size={14} className="ico"/>Select Path</span>
      <span className="iai-btn" onClick={onClose}><Icon name="feather" size={14} className="ico"/>Rewrite</span>
    </span>
  );
}

function MuseChat() {
  const [draft, setDraft] = React.useState('');
  const [chat, setChat] = React.useState([
    { who: 'user', text: 'Outline the conflict in this scene.' },
    { who: 'muse', text: 'In Chapter 3, Elara encounters the Guardian. The clash is both physical and ideological — she must prove her worthiness against the echo of her own doubts. Consider sharpening the stakes by giving the Guardian a line that mirrors her internal monologue.' },
    { who: 'user', text: 'What would the Guardian say?' },
    { who: 'muse', text: 'Try: "You came seeking the blade, child. But the blade has been seeking you longer." Cold, neutral, almost compassionate. Echoes Aethel\u2019s prophecy from the Prologue.' },
  ]);
  const logRef = React.useRef(null);

  React.useEffect(() => {
    if (logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight;
  }, [chat]);

  const send = () => {
    if (!draft.trim()) return;
    setChat(c => [...c, { who: 'user', text: draft }]);
    setDraft('');
    setTimeout(() => {
      setChat(c => [...c, { who: 'muse', text: 'A possible thread: tie the labyrinth\u2019s shifting walls to Elara\u2019s indecision. Each wrong turn is a memory she\u2019d rather forget.' }]);
    }, 700);
  };

  return (
    <aside className="muse">
      <div className="muse-head">
        <span className="ember-wrap"><Ember size={16}/></span>
        <span className="title">Mythos AI Assistant</span>
        <span className="actions">
          <Icon name="history" size={14}/>
          <Icon name="reset" size={14}/>
        </span>
      </div>
      <div className="muse-log" ref={logRef}>
        {chat.map((m, i) => (
          <div key={i} className={`msg ${m.who}`}>
            <div className="avatar">
              {m.who === 'user' ? <Icon name="users" size={12}/> : <Ember size={12}/>}
            </div>
            <div className="body">
              <div className="who">{m.who === 'user' ? 'User' : 'AI Assistant'}</div>
              <div className="bubble">{m.text}</div>
            </div>
          </div>
        ))}
      </div>
      <div className="muse-input">
        <div className="field">
          <input
            placeholder="Ask Mythos\u2026"
            value={draft}
            onChange={e => setDraft(e.target.value)}
            onKeyDown={e => e.key === 'Enter' && send()}
          />
          <div className="row">
            <span className="model">Claude · Sonnet</span>
            <button className="btn gold send" onClick={send}>Send</button>
          </div>
        </div>
      </div>
    </aside>
  );
}

function ProjectTree({ project, document, variables }) {
  return (
    <aside className="project-tree">
      <div className="tree-section">
        <h4>Project Tree <span className="add"><Icon name="plus" size={11}/></span></h4>
        <div className="tree-row">
          <span className="ico"><Icon name="book" size={13}/></span>
          {project}
        </div>
        <div className="tree-row nested">
          <span className="ico"><Icon name="chevron-down" size={11}/></span>
          Drafts
        </div>
        <div className="tree-row nested-2 active">
          <span className="ico"><Icon name="feather" size={11}/></span>
          {document?.title || 'Chapter 3.md'}
          <span className="meta">{document?.wordCount || '1.2k'}</span>
        </div>
        <div className="tree-row nested-2">Chapter 1.md</div>
        <div className="tree-row nested-2">Chapter 2.md</div>
        <div className="tree-row nested">Research</div>
        <div className="tree-row nested">Assets</div>
        <div className="tree-row nested">Notes</div>
      </div>

      <div className="tree-section">
        <h4>Outline</h4>
        <div className="tree-row nested">§ Arrival</div>
        <div className="tree-row nested">§ The Market</div>
        <div className="tree-row nested active">
          <span className="ico"><Icon name="chevron-right" size={11}/></span>
          § The Inn
        </div>
        <div className="tree-row nested">§ The Guardian</div>
      </div>

      <div className="tree-section">
        <h4>Variables <span className="add">{variables.length}</span></h4>
        {variables.slice(0, 6).map(variable => (
          <div key={variable.id} className="tree-row nested" style={{fontFamily:'var(--font-mono)',fontSize:11}}>
            {variable.token}
          </div>
        ))}
      </div>
    </aside>
  );
}

export function EditorScreen({ tokenStyle, project, projectData, onDocumentChange }) {
  const [showAI, setShowAI] = React.useState(false);
  const document = activeDocument(projectData) || activeDocument(FALLBACK_PROJECT);
  const variables = projectData?.variables?.length ? projectData.variables : FALLBACK_VARIABLES;
  const handleDocumentChange = React.useCallback((patch) => {
    if (!document?.id) return;
    onDocumentChange?.(document.id, patch);
  }, [document?.id, onDocumentChange]);

  return (
    <div className="editor" data-token-style={tokenStyle}>
      <ProjectTree project={project} document={document} variables={variables}/>
      <section className="ms-wrap">
        <div className="ms-breadcrumb">
          <Icon name="home" size={13}/>
          <span>Mythos</span>
          <span className="sep">/</span>
          <span>{project}</span>
          <span className="sep">/</span>
          <span>{document?.path?.split('/')[0] || 'Drafts'}</span>
          <span className="sep">/</span>
          <span className="here">{document?.title || 'Chapter 3 — The Labyrinth'}</span>
          <span className="right">
            <button className="btn" onClick={() => setShowAI(s => !s)}>
              <Icon name="sparkle" size={12}/> AI
            </button>
            <button className="btn ghost"><Icon name="eye" size={12}/> Preview</button>
          </span>
        </div>

        <div className="ms-scroll">
          <div className="ms-page">
            <div className="ms-eyebrow">Part I · The Call</div>
            <div className="ms-chapter-glyph">
              <span className="glyph"><Icon name="sigil-flame" size={28}/></span>
              <h1 className="ms-chapter">{document?.title || 'Chapter III · The Labyrinth'}</h1>
            </div>

            <div className="manuscript-body" style={{
              fontSize: 'var(--ms-font-size, 19px)',
              lineHeight: 'var(--ms-line-height, 1.78)',
            }}>
              <MythosEditor
                document={document}
                variables={variables}
                onDocumentChange={handleDocumentChange}
              />
              {showAI && <InlineAIMenu onClose={() => setShowAI(false)}/>}
            </div>
          </div>
        </div>

        <div className="statusbar">
          <span className="gold">The Ledger</span>
          <span className="sep">|</span>
          <span className="word-count">{document?.wordCount || 0} words</span>
          <span className="sep">|</span>
          <span>Exploration: <span className="gold">The Shattered Isles</span></span>
          <span className="right">
            <span className="dot"/>
            <span>Synced · Just now</span>
            <span className="sep">|</span>
            <span>Claude · Sonnet</span>
          </span>
        </div>
      </section>

      <MuseChat/>
    </div>
  );
}

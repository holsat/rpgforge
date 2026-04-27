import React from 'react';
import { Icon, Ember } from '../components/icons.jsx';

// Mythos — Editor screen (Draft view)
// Manuscript with variable autocomplete + inline AI menu + Muse sidebar

const VARIABLES = [
  { name: '[@ElaraVance]',  kind: 'Character' },
  { name: '[@HighpassInn]', kind: 'Location' },
  { name: '[@Aethelgard]',  kind: 'Location' },
  { name: '[@DuskWar]',     kind: 'Concept' },
  { name: '[@TheShard]',    kind: 'Artifact' },
  { name: '[@Kaelen]',      kind: 'Character' },
];

function Tok({ children, style = 'pill', onClick }) {
  return <span className={`tok style-${style}`} onClick={onClick}>{children}</span>;
}

function VariableAutocomplete({ tokenStyle }) {
  const [active, setActive] = React.useState(0);
  return (
    <span className="autocomplete" style={{ left: '50%', top: '6px' }}>
      <span className="ac-list">
        {VARIABLES.map((v, i) => (
          <span key={v.name} className={'ac-row' + (i === active ? ' active' : '')}
               onMouseEnter={() => setActive(i)}>
            <span className="num">{i + 1}.</span>
            <span className="name">{v.name}</span>
            <span className="kind">{v.kind}</span>
          </span>
        ))}
      </span>
    </span>
  );
}

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

function ProjectTree({ project }) {
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
        <div className="tree-row nested-2">Chapter 1.md</div>
        <div className="tree-row nested-2">Chapter 2.md</div>
        <div className="tree-row nested-2 active">
          <span className="ico"><Icon name="feather" size={11}/></span>
          Chapter 3.md
          <span className="meta">1.2k</span>
        </div>
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
        <h4>Variables <span className="add">{VARIABLES.length}</span></h4>
        <div className="tree-row nested" style={{fontFamily:'var(--font-mono)',fontSize:11}}>[@ElaraVance]</div>
        <div className="tree-row nested" style={{fontFamily:'var(--font-mono)',fontSize:11}}>[@HighpassInn]</div>
        <div className="tree-row nested" style={{fontFamily:'var(--font-mono)',fontSize:11}}>[@Aethelgard]</div>
        <div className="tree-row nested" style={{fontFamily:'var(--font-mono)',fontSize:11}}>[@DuskWar]</div>
      </div>
    </aside>
  );
}

export function EditorScreen({ tokenStyle, project }) {
  const [showAC, setShowAC] = React.useState(true);
  const [showAI, setShowAI] = React.useState(false);

  return (
    <div className="editor">
      <ProjectTree project={project}/>
      <section className="ms-wrap">
        <div className="ms-breadcrumb">
          <Icon name="home" size={13}/>
          <span>Mythos</span>
          <span className="sep">/</span>
          <span>{project}</span>
          <span className="sep">/</span>
          <span>Drafts</span>
          <span className="sep">/</span>
          <span className="here">Chapter 3 — The Labyrinth</span>
          <span className="right">
            <button className="btn" onClick={() => { setShowAC(false); setShowAI(s => !s); }}>
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
              <h1 className="ms-chapter">Chapter III · The Labyrinth</h1>
            </div>

            <div className="manuscript-body" style={{
              fontSize: 'var(--ms-font-size, 19px)',
              lineHeight: 'var(--ms-line-height, 1.78)',
            }}>
              <p>
                The cobblestones of Oakhaven were slick with evening rain.{' '}
                <Tok style={tokenStyle}>[@ElaraVance]</Tok> pulled her cloak tighter, her eyes scanning the shadowy market stalls. Her goal lay beyond, in the mysterious <Tok style={tokenStyle}>[@HighpassInn]</Tok>. The air was thick with tension; the whispers of the looming <Tok style={tokenStyle}>[@DuskWar]</Tok> hung heavy upon every threshold.
              </p>
              <p style={{position:'relative'}}>
                At the end of the alley, where a single lantern threw its small{' '}
                <span className="selected-word">amber</span>{showAI && <InlineAIMenu onClose={() => setShowAI(false)}/>} circle against the wet stone, she paused. The cursor of memory hovered over a name she had not spoken aloud since <em>Aethel</em>: <Tok style={tokenStyle}>[@Highpass</Tok>{showAC && <span style={{position:'relative',display:'inline-block'}}><span className="caret"/><VariableAutocomplete tokenStyle={tokenStyle}/></span>}
              </p>
              <p>
                She drew breath and stepped past the threshold. Inside, the firelight made an island of warmth amid the chill. <span className="place">Brom the innkeep</span> looked up, his eye catching the silver clasp at her throat — the same clasp her mother had pressed into her hand the night the <Tok style={tokenStyle}>[@DuskWar]</Tok> began.
              </p>
              <p>
                <em>Heading Stalls.</em> She ordered ale she would not drink and watched the door. The <Tok style={tokenStyle}>[@TheShard]</Tok> was here, somewhere — bound, she suspected, to the corner table where a hooded figure sat with their back to the room. She had crossed three kingdoms for this moment. She would not waste it on hesitation.
              </p>
            </div>
          </div>
        </div>

        <div className="statusbar">
          <span className="gold">The Ledger</span>
          <span className="sep">|</span>
          <span className="word-count">1,224 words</span>
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

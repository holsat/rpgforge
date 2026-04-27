import React from 'react';
import { Icon } from '../components/icons.jsx';

// Mythos — Settings (Muse AI configuration)

export function SettingsScreen() {
  const [section, setSection] = React.useState('muse');
  const [model, setModel] = React.useState('Anthropic (Claude Sonnet 4.5)');
  const [ctx, setCtx] = React.useState(32);
  const [lumina, setLumina] = React.useState(true);
  const [cloud, setCloud] = React.useState(false);

  return (
    <div className="settings">
      <aside className="settings-nav">
        <div className="head">MYTHOS</div>
        {[
          {id:'scribe', name:'Scribe', sub:'Project',     icon:'feather'},
          {id:'muse',   name:'Muse',   sub:'AI',          icon:'sigil-flame'},
          {id:'lumina', name:'Lumina', sub:'Focus Mode',  icon:'sparkle'},
          {id:'atlas',  name:'Atlas',  sub:'Maps',        icon:'map'},
          {id:'export', name:'Export', sub:'',            icon:'export'},
        ].map(it => (
          <div key={it.id}
               className={'settings-nav-item' + (it.id === section ? ' active' : '')}
               onClick={() => setSection(it.id)}>
            <span className="ico"><Icon name={it.icon} size={16}/></span>
            <div style={{display:'flex', flexDirection:'column'}}>
              <span>{it.name}</span>
              {it.sub && <span style={{fontSize:10, opacity:0.6, letterSpacing:'0.06em'}}>{it.sub}</span>}
            </div>
          </div>
        ))}
      </aside>

      <section className="settings-content">
        <h1>Muse — AI Settings</h1>
        <div className="lede">Configure your Muse AI integration for creative assistance.</div>

        <div className="settings-section">
          <h3>AI Configuration</h3>
          <div className="field-group">
            <div className="field">
              <label>API Key</label>
              <div className="input-with-eye">
                <input type="password" placeholder="Paste your API key"/>
                <span className="eye"><Icon name="eye" size={14}/></span>
              </div>
              <span className="hint">Enter your AI service key. Stored locally and encrypted.</span>
            </div>

            <div className="field">
              <label>Model Selection</label>
              <select value={model} onChange={e => setModel(e.target.value)}>
                <option>OpenAI (GPT-5)</option>
                <option>Anthropic (Claude Sonnet 4.5)</option>
                <option>Anthropic (Claude Opus 4)</option>
                <option>Local (Ollama · llama-3.2)</option>
              </select>
            </div>

            <div className="field slider-field">
              <label>Context Length · {ctx}K</label>
              <div className="row">
                <input type="range" min="4" max="200" value={ctx}
                       onChange={e => setCtx(+e.target.value)}/>
                <span className="value">{(ctx*1000).toLocaleString()} tokens</span>
              </div>
              <span className="hint">Adjust the AI context memory depth.</span>
            </div>
          </div>
        </div>

        <div className="settings-section">
          <h3>Integration Details</h3>
          <div className="toggle-row">
            <div className="text">
              <span className="name">Lumina Focus Mode</span>
              <span className="desc">Enable Lumina Focus Mode when Muse is active.</span>
            </div>
            <span className={'tg-switch' + (lumina ? ' on' : '')} onClick={() => setLumina(v => !v)}><i/></span>
          </div>
          <div className="toggle-row">
            <div className="text">
              <span className="name">Cloud Sync</span>
              <span className="desc">Sync project state and chat history across devices.</span>
            </div>
            <span className={'tg-switch' + (cloud ? ' on' : '')} onClick={() => setCloud(v => !v)}><i/></span>
          </div>
        </div>

        <div style={{display:'flex', gap:12, marginTop:24}}>
          <button className="btn gold">Save Changes</button>
          <button className="btn">Cancel</button>
        </div>
      </section>
    </div>
  );
}

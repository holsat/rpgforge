import React from 'react';
import { Icon, Ember } from '../components/icons.jsx';

// Mythos — Codex (character dossier)

const CHARACTERS = [
  { id:'lirael', name:'Lirael',   role:'Elven Warrior', glyph:'LI', active:true },
  { id:'kaelen', name:'Kaelen',   role:'Shadowblade',   glyph:'KA' },
  { id:'aria',   name:'Aria',     role:'Mage',          glyph:'AR' },
  { id:'brom',   name:'Brom',     role:'Warrior',       glyph:'BR' },
  { id:'rhion',  name:'Rhion',    role:'Oracle',        glyph:'RH' },
  { id:'lyra',   name:'Lyra',     role:'Rogue',         glyph:'LY' },
  { id:'silas',  name:'Silas',    role:'Warlock',       glyph:'SI' },
];
const LOCATIONS = [
  { id:'aethelgard', name:'Aethelgard', role:'Capital City' },
  { id:'highpass',   name:'Highpass Inn', role:'Crossroads' },
  { id:'shadowvale', name:'Shadowvale', role:'Hidden Valley' },
];

function StatBar({ name, pct }) {
  return (
    <div className="stat-row">
      <span className="name">{name}</span>
      <span className="bar"><span className="fill" style={{width: pct + '%'}}/></span>
      <span className="pct">{pct}%</span>
    </div>
  );
}

export function CodexScreen() {
  const [active, setActive] = React.useState('lirael');
  return (
    <div className="codex">
      <aside className="codex-list">
        <div className="codex-search">
          <Icon name="search" size={13}/>
          <input placeholder="Search the Codex…"/>
        </div>

        <div className="codex-cat">
          <Icon name="users" size={11}/> Characters <span className="count">{CHARACTERS.length}</span>
        </div>
        {CHARACTERS.map(c => (
          <div key={c.id} className={'codex-entry' + (c.id === active ? ' active' : '')}
               onClick={() => setActive(c.id)}>
            <div className="frame">{c.glyph}</div>
            <div className="info">
              <div className="name">{c.name}</div>
              <div className="role">{c.role}</div>
            </div>
            {c.id === active && <Icon name="users" size={12}/>}
          </div>
        ))}

        <div className="codex-cat">
          <Icon name="map" size={11}/> Locations <span className="count">{LOCATIONS.length}</span>
        </div>
        {LOCATIONS.map(l => (
          <div key={l.id} className="codex-entry">
            <div className="frame"><Icon name="pin" size={14}/></div>
            <div className="info">
              <div className="name">{l.name}</div>
              <div className="role">{l.role}</div>
            </div>
          </div>
        ))}
      </aside>

      <section className="dossier">
        <div className="dossier-bar">
          <div className="crumb">
            The Codex <span style={{opacity:0.4, margin:'0 6px'}}>›</span>
            Characters <span style={{opacity:0.4, margin:'0 6px'}}>›</span>
            <span className="here">Lirael</span>
          </div>
          <div className="right">
            <button className="btn ghost"><Icon name="graph" size={12}/> Relationship Map</button>
            <button className="btn"><Icon name="edit" size={12}/> Edit</button>
            <button className="btn"><Icon name="history" size={12}/> History</button>
            <button className="btn"><Icon name="share" size={12}/> Share</button>
          </div>
        </div>

        <div className="dossier-scroll">
          <div className="dossier-hero">
            <div className="portrait-frame">
              <Icon name="corner" className="portrait-corner tl" size={26}/>
              <Icon name="corner" className="portrait-corner tr" size={26}/>
              <Icon name="corner" className="portrait-corner bl" size={26}/>
              <Icon name="corner" className="portrait-corner br" size={26}/>
              <div className="portrait-fill">
                <div className="portrait-silhouette">
                  <svg viewBox="0 0 100 130" width="100%" height="100%" fill="none">
                    <ellipse cx="50" cy="34" rx="14" ry="17" fill="#0a0805" opacity="0.95"/>
                    <path d="M30 130 Q30 76 50 70 Q70 76 70 130 Z" fill="#0a0805" opacity="0.95"/>
                    <path d="M40 56 Q50 50 60 56 L62 80 L38 80 Z" fill="#0a0805" opacity="0.95"/>
                    <path d="M50 30 L46 18 L52 22 L54 16 L56 22 L60 18 L58 30" stroke="#D4AF37" strokeWidth="0.6" fill="none" opacity="0.7"/>
                  </svg>
                </div>
              </div>
            </div>

            <div className="dossier-header">
              <h1>DOSSIER</h1>
              <div className="subtitle">Lirael of the Silverwood, Sworn Blade of the Verdant Court</div>

              <div className="stats">
                <StatBar name="Strength"     pct={85}/>
                <StatBar name="Intelligence" pct={92}/>
                <StatBar name="Agility"      pct={95}/>
                <StatBar name="Endurance"    pct={78}/>
                <StatBar name="Mysticism"    pct={88}/>
              </div>

              <div className="traits-grid">
                <div className="trait-block">
                  <span className="label-caps">Key Traits</span>
                  <div className="trait-pills">
                    <span className="pill">Vigilant</span>
                    <span className="pill">Faithful</span>
                    <span className="pill">Bladekin</span>
                  </div>
                </div>
                <div className="trait-block">
                  <span className="label-caps">Status</span>
                  <div className="trait-pills">
                    <span className="pill status">Active · Ch. 3</span>
                  </div>
                </div>
              </div>
            </div>
          </div>

          <div className="dossier-body">
            <div className="body-col">
              <h2>Biography <button className="btn ghost" style={{marginLeft:'auto', height:24, fontSize:10, padding:'0 10px'}}><Ember size={11}/> Muse</button></h2>
              <p>
                Lirael was born to the silverwood houses of <strong>Garadond</strong>, third daughter of a mother who had once ridden the <em>Verdant Wars</em>. She came of age in the long quiet that followed, and so she practiced the blade against shadow rather than steel — her brothers laughed; her mother did not.
              </p>
              <p>
                When the <strong>Dusk War</strong> broke, Lirael was the first of her line to take the Sworn Oath. She left the silverwood with nothing but the Sunstone Blade and a folded letter she has never read, and she has not returned since.
              </p>
              <p>
                She fights as one who has rehearsed grief in private — economical, patient, beautiful. The court calls her the <em>Last Verdant</em>, but she does not answer to it.
              </p>
            </div>

            <div className="body-col">
              <h2>Lore &amp; Legend</h2>
              <p>
                <strong>Weapon —</strong> The Sunstone Blade. Forged in the embers of the first manifest dawn; said to weep light when held by the unworthy.
              </p>
              <p>
                <strong>Artifacts —</strong> The Sunstone Blade, a folded letter, a sliver of obsidian taken from the Citadel of Ash.
              </p>
              <p>
                <strong>Myths —</strong> Lirael is whispered, in the older provinces, to be the diarchor of the <em>Treaty Broken</em>; any mythical blade dies from her hand.
              </p>
              <p>
                <strong>Allegiance —</strong> The Verdant Court · The Sworn Blade · House Garadond.
              </p>
            </div>
          </div>
        </div>
      </section>
    </div>
  );
}

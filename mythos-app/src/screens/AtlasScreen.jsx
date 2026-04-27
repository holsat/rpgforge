import React from 'react';
import { Icon } from '../components/icons.jsx';

// Mythos — Atlas (illustrated map)

export function AtlasScreen() {
  const [active, setActive] = React.useState('shadowvale');

  return (
    <div className="atlas">
      <aside className="codex-list">
        <div className="codex-search">
          <Icon name="search" size={13}/>
          <input placeholder="Search the Atlas…"/>
        </div>
        <div className="codex-cat"><Icon name="map" size={11}/> Regions</div>
        <div className="codex-entry"><div className="frame"><Icon name="pin" size={14}/></div>
          <div className="info"><div className="name">Aethelgard</div><div className="role">Realm</div></div>
        </div>
        <div className="codex-cat"><Icon name="pin" size={11}/> Locations</div>
        {[
          {id:'shadowvale', name:'Shadowvale', role:'Hidden Valley'},
          {id:'iron',       name:'Iron Citadel', role:'Fortress'},
          {id:'eldoria',    name:'Eldoria', role:'Capital'},
          {id:'whisper',    name:'Whisperwood', role:'Forest'},
        ].map(l => (
          <div key={l.id} className={'codex-entry' + (l.id === active ? ' active' : '')}
               onClick={() => setActive(l.id)}>
            <div className="frame"><Icon name="pin" size={14}/></div>
            <div className="info"><div className="name">{l.name}</div><div className="role">{l.role}</div></div>
          </div>
        ))}
      </aside>

      <section className="atlas-stage">
        {/* Parchment-style placeholder map (SVG) */}
        <svg className="map-svg" viewBox="0 0 800 600" preserveAspectRatio="xMidYMid slice">
          <defs>
            <radialGradient id="parch" cx="50%" cy="40%" r="80%">
              <stop offset="0%"  stopColor="#3d3528"/>
              <stop offset="60%" stopColor="#2a2419"/>
              <stop offset="100%" stopColor="#14110a"/>
            </radialGradient>
            <pattern id="paper" width="100" height="100" patternUnits="userSpaceOnUse">
              <rect width="100" height="100" fill="url(#parch)"/>
              <circle cx="20" cy="30" r="0.5" fill="#D4AF37" opacity="0.06"/>
              <circle cx="70" cy="60" r="0.5" fill="#D4AF37" opacity="0.06"/>
              <circle cx="50" cy="80" r="0.4" fill="#D4AF37" opacity="0.05"/>
            </pattern>
          </defs>
          <rect width="800" height="600" fill="url(#paper)"/>
          {/* Coastline */}
          <path d="M40,400 Q120,380 180,420 Q260,460 360,440 Q460,420 540,460 Q640,500 760,470 L760,600 L40,600 Z"
                fill="#0e1a24" opacity="0.55" stroke="#D4AF37" strokeWidth="0.6" strokeOpacity="0.4"/>
          {/* Mountains */}
          {Array.from({length:14}).map((_, i) => {
            const x = 100 + i * 45 + (i%2)*15;
            const y = 180 + (i%3)*30;
            return <path key={i} d={`M${x},${y} l15,-30 l15,30 z`} fill="#2a2218" stroke="#D4AF37" strokeWidth="0.6" strokeOpacity="0.4"/>;
          })}
          {/* Forest blobs */}
          {Array.from({length:18}).map((_, i) => (
            <circle key={i} cx={120 + (i%6)*90 + (i*13)%30} cy={300 + Math.floor(i/6)*40} r="6"
                    fill="#1f2a18" stroke="#D4AF37" strokeWidth="0.4" strokeOpacity="0.3"/>
          ))}
          {/* River */}
          <path d="M200,150 Q250,250 300,300 Q330,400 280,500" stroke="#3a5266" strokeWidth="2" fill="none" opacity="0.7"/>
          {/* Title cartouche */}
          <g transform="translate(120,80)">
            <rect x="-8" y="-22" width="160" height="34" rx="3" fill="#1a140a" stroke="#D4AF37" strokeWidth="0.8" opacity="0.85"/>
            <text x="72" y="0" textAnchor="middle" fontFamily="Cinzel, serif" fontSize="16" fill="#D4AF37" letterSpacing="2">AETHELGARD</text>
          </g>
          {/* Region labels */}
          <text x="220" y="240" fontFamily="EB Garamond, serif" fontStyle="italic" fontSize="14" fill="#D4AF37" opacity="0.7">Iron Citadel</text>
          <text x="380" y="320" fontFamily="EB Garamond, serif" fontStyle="italic" fontSize="14" fill="#D4AF37" opacity="0.7">Whisperwood</text>
          <text x="380" y="510" fontFamily="EB Garamond, serif" fontStyle="italic" fontSize="14" fill="#D4AF37" opacity="0.7">Eldoria</text>
          <text x="120" y="540" fontFamily="EB Garamond, serif" fontStyle="italic" fontSize="13" fill="#D4AF37" opacity="0.6">Sunken Isles</text>
        </svg>

        {/* Pins */}
        <div className="map-pin" style={{ left: '32%', top: '46%' }}>
          <Icon name="pin" size={26} color="#D4AF37"/>
          <span className="label">Iron Citadel</span>
        </div>
        <div className={'map-pin' + (active === 'shadowvale' ? ' active' : '')}
             style={{ left: '58%', top: '52%' }} onClick={() => setActive('shadowvale')}>
          <Icon name="pin" size={32} color="#E8C547"/>
          <span className="label">Shadowvale</span>
        </div>
        <div className="map-pin" style={{ left: '46%', top: '78%' }}>
          <Icon name="pin" size={26} color="#D4AF37"/>
          <span className="label">Eldoria</span>
        </div>

        {/* Location card */}
        {active === 'shadowvale' && (
          <div className="location-card">
            <div className="head">
              <Icon name="lantern" size={14}/>
              <span className="label">Librarian</span>
            </div>
            <div style={{padding:'4px 16px 0', fontFamily:'var(--font-sans)', fontSize:10, letterSpacing:'0.16em', textTransform:'uppercase', color:'var(--fg-3)'}}>Location Data</div>
            <h3>SHADOWVALE</h3>
            <div className="desc">
              A hidden valley shrouded in mist, where the moon\u2019s curse first took root. Long silent. Said to remember every footstep that has ever crossed its boundary.
            </div>
            <div className="vars">
              <div className="var-line"><span className="k">[Status:</span><span className="v">Active]</span></div>
              <div className="var-line"><span className="k">[Region:</span><span className="v">Nightwood]</span></div>
              <div className="var-line"><span className="k">[Lore:</span><span className="v">Curse of the Moon]</span></div>
            </div>
          </div>
        )}
      </section>

      <aside className="atlas-vars">
        <div className="head">
          <span>Variables</span>
          <Icon name="filter" size={12}/>
        </div>
        <div style={{
          padding:'4px 10px', fontFamily:'var(--font-sans)', fontSize:10,
          letterSpacing:'0.1em', color:'var(--fg-3)', textTransform:'uppercase'
        }}>Located here · Shadowvale</div>
        {['Elara Vance','Nightwood Crystal','Shadowfang Cloak','Kaelen\u2019s Journal'].map(v => (
          <div key={v} className="var-row">
            <span className="ico"><Icon name="sparkle" size={11}/></span>
            <span>{v}</span>
          </div>
        ))}
      </aside>
    </div>
  );
}

import React from 'react';
import { Icon } from '../components/icons.jsx';

// Mythos — Timeline (multi-thread chronology)

export function TimelineScreen() {
  const W = 1100; // canvas width estimate
  const chapters = [
    { num:'Prologue',  title:'The Oracle',    x: 0.10 },
    { num:'Chapter 1', title:'Aethel\u2019s Vow', x: 0.27 },
    { num:'Chapter 2', title:'The Serpent',   x: 0.44 },
    { num:'Chapter 3', title:'The Labyrinth', x: 0.61 },
    { num:'Chapter 4', title:'War Drums',     x: 0.78 },
  ];
  const aethelNodes = [
    { x: 0.12, icon:'sigil-tree' },
    { x: 0.30, icon:'sigil-eye' },
    { x: 0.61, icon:'sigil-flame', active:true },
    { x: 0.80, icon:'sigil-blade' },
  ];
  const elaraNodes = [
    { x: 0.20, icon:'sigil-rune' },
    { x: 0.46, icon:'sigil-eye' },
    { x: 0.61, icon:'sigil-flame', active:true },
    { x: 0.74, icon:'sigil-crown' },
  ];

  return (
    <div className="timeline">
      <div className="timeline-bar">
        <h2>Timeline</h2>
        <div className="right">
          <button className="btn ghost"><Icon name="reset" size={12}/> Reset</button>
          <button className="btn gold"><Icon name="pin" size={12}/> Set Today</button>
        </div>
      </div>

      <div className="timeline-canvas">
        {/* Today vertical line */}
        <div className="timeline-today" style={{ left: '61%' }}>
          <span className="label">TODAY <span className="year">1006 CE</span></span>
        </div>

        {/* Thread 1: Aethel */}
        <div className="thread-label" style={{ left: 16, top: 40 }}>
          <span className="eyebrow">Thread 1</span>
          <span className="name">AETHEL</span>
          <span className="sigil">Aethel\u2019s sigil</span>
        </div>

        {aethelNodes.map((n, i) => (
          <div key={'a'+i} className={'tl-node' + (n.active ? ' active' : '')}
               style={{ left: `calc(${n.x * 100}% - 18px)`, top: 60 }}>
            <Icon name={n.icon} size={18}/>
          </div>
        ))}

        {/* Connecting line for Aethel */}
        <svg style={{position:'absolute', left:0, top:0, width:'100%', height:'100%', pointerEvents:'none'}}>
          <line x1="6%" y1="78" x2="94%" y2="78" stroke="rgba(212,175,55,0.35)" strokeWidth="1" strokeDasharray="3 4"/>
          <line x1="6%" y1="320" x2="94%" y2="320" stroke="rgba(212,175,55,0.35)" strokeWidth="1" strokeDasharray="3 4"/>
        </svg>

        {/* Chapter strip in middle */}
        <div className="tl-chapter-strip" style={{ left: '6%', right: '6%', top: 160 }}>
          {chapters.map((c) => (
            <div key={c.num} className="tl-chapter">
              <span className="num">{c.num}</span>
              <span className="title">{c.title}</span>
            </div>
          ))}
        </div>

        {/* Year axis */}
        <div className="tl-axis" style={{ left: '6%', right: '6%', top: 256 }}>
          {['980','990','1000','1010','1020'].map((y, i) => (
            <span key={y} className="tl-tick major" style={{ left: `${i * 25}%` }}>
              <span className="yr">{y} CE</span>
            </span>
          ))}
          {Array.from({length: 20}).map((_, i) => (
            <span key={'t'+i} className="tl-tick" style={{ left: `${i * 5}%` }}/>
          ))}
        </div>

        {/* Thread 2: Elara */}
        <div className="thread-label" style={{ left: 16, top: 300 }}>
          <span className="eyebrow">Thread 2</span>
          <span className="name">ELARA</span>
          <span className="sigil">Elara\u2019s sigil</span>
        </div>

        {elaraNodes.map((n, i) => (
          <div key={'e'+i} className={'tl-node' + (n.active ? ' active' : '')}
               style={{ left: `calc(${n.x * 100}% - 18px)`, top: 302 }}>
            <Icon name={n.icon} size={18}/>
          </div>
        ))}

        <div className="timeline-zoom">
          <span>ZOOM</span>
          <input type="range" min="0" max="100" defaultValue="40"/>
          <span style={{color:'var(--mythos-gold-deep)', fontFamily:'var(--font-mono)'}}>980 — 1015</span>
        </div>
      </div>
    </div>
  );
}

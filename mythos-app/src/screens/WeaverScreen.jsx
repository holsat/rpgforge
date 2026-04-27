import React from 'react';
import { Icon } from '../components/icons.jsx';

// Mythos — Story Weaver (force-directed graph view)

const NODES = [
  { id:'thread', label:'The Golden Thread', x:0.50, y:0.50, r:50, core:true, sub:'Core Idea · 24px' },
  { id:'seers',  label:'The Seer\u2019s Warning', x:0.34, y:0.30, r:34, sub:'Glow · 18px' },
  { id:'choice', label:'Aethelred\u2019s Choice', x:0.66, y:0.30, r:34, sub:'Glow · 18px' },
  { id:'temple', label:'The Ruined Temple', x:0.30, y:0.72, r:34, sub:'Glow · 18px' },
  { id:'exile',  label:'Orson\u2019s Exile', x:0.62, y:0.72, r:34, sub:'Glow · 18px' },
  { id:'oracle', label:'The Oracle Speaks', x:0.18, y:0.18, r:22 },
  { id:'cursed', label:'The Cursed Scroll', x:0.13, y:0.34, r:20 },
  { id:'meet1',  label:'Meeting Elara',     x:0.30, y:0.48, r:18 },
  { id:'meet2',  label:'Meeting Aethel',    x:0.40, y:0.40, r:18 },
  { id:'betray', label:'Aethelred\u2019s Betrayal', x:0.82, y:0.18, r:22 },
  { id:'orson',  label:'Orson\u2019s Exile (alt)', x:0.85, y:0.36, r:20 },
  { id:'treaty', label:'The Treaty Broken', x:0.90, y:0.55, r:22 },
  { id:'forest', label:'The Forest Path',   x:0.18, y:0.86, r:20 },
  { id:'sacri',  label:'Aethelred\u2019s Sacrifice', x:0.34, y:0.92, r:20 },
  { id:'forbid', label:'Forbidden Knowledge', x:0.50, y:0.92, r:20 },
  { id:'hidden', label:'The Hidden Map',    x:0.74, y:0.78, r:20 },
  { id:'ritual', label:'The Ritual Begins', x:0.66, y:0.92, r:20 },
  { id:'ancient',label:'Ancient Whispers',  x:0.86, y:0.78, r:20 },
];
const LINKS = [
  ['thread','seers'],['thread','choice'],['thread','temple'],['thread','exile'],
  ['thread','meet1'],['thread','meet2'],
  ['seers','oracle'],['seers','cursed'],['seers','meet2'],
  ['choice','betray'],['choice','orson'],['choice','treaty'],
  ['exile','orson'],['exile','treaty'],['exile','hidden'],['exile','ancient'],
  ['temple','forest'],['temple','sacri'],['temple','forbid'],['temple','meet1'],
  ['hidden','ritual'],['ritual','ancient'],
];

export function WeaverScreen() {
  const [hover, setHover] = React.useState(null);
  const node = (id) => NODES.find(n => n.id === id);

  return (
    <div className="weaver">
      <div className="weaver-bar">
        <Icon name="graph" size={16} color="#D4AF37"/>
        <span className="title">Story Weaver — Graph View</span>
        <div className="right">
          <button className="btn"><Icon name="reset" size={12}/> Save</button>
          <button className="btn"><Icon name="share" size={12}/> Share</button>
          <button className="btn ghost"><Icon name="sparkle" size={12}/> Focus Mode</button>
          <button className="btn"><Icon name="export" size={12}/> Export</button>
        </div>
      </div>

      <div className="weaver-canvas">
        <div className="weaver-title">
          <div className="eyebrow">Mythos Exploration</div>
          <h2>The Weaver\u2019s Prophecy</h2>
        </div>

        <svg className="graph-svg" viewBox="0 0 1000 700" preserveAspectRatio="xMidYMid meet">
          <defs>
            <radialGradient id="goldNode" cx="50%" cy="40%" r="60%">
              <stop offset="0%"  stopColor="#F4D87A"/>
              <stop offset="40%" stopColor="#D4AF37"/>
              <stop offset="100%" stopColor="#5a4818"/>
            </radialGradient>
            <radialGradient id="coreNode" cx="50%" cy="40%" r="60%">
              <stop offset="0%"  stopColor="#FFE9A0"/>
              <stop offset="50%" stopColor="#E8C547"/>
              <stop offset="100%" stopColor="#7a5e1c"/>
            </radialGradient>
            <filter id="goldGlow">
              <feGaussianBlur stdDeviation="6" result="b"/>
              <feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge>
            </filter>
          </defs>

          {/* Links */}
          {LINKS.map(([a, b], i) => {
            const A = node(a), B = node(b);
            return <line key={i}
              x1={A.x*1000} y1={A.y*700} x2={B.x*1000} y2={B.y*700}
              stroke="rgba(212,175,55,0.28)" strokeWidth="0.8"/>;
          })}

          {/* Nodes */}
          {NODES.map(n => {
            const cx = n.x*1000, cy = n.y*700;
            const fill = n.core ? 'url(#coreNode)' : 'url(#goldNode)';
            return (
              <g key={n.id}
                 onMouseEnter={() => setHover(n.id)}
                 onMouseLeave={() => setHover(null)}
                 style={{cursor:'pointer'}}>
                <circle cx={cx} cy={cy} r={n.r + 8} fill="rgba(212,175,55,0.15)" filter="url(#goldGlow)"/>
                <circle cx={cx} cy={cy} r={n.r} fill={fill}
                        stroke="rgba(212,175,55,0.7)" strokeWidth="1"/>
                <circle cx={cx} cy={cy} r={n.r-6} fill="none"
                        stroke="rgba(255,233,160,0.25)" strokeWidth="0.6"/>
                {n.r >= 30 && (
                  <text x={cx} y={cy-2} textAnchor="middle"
                        fontFamily="EB Garamond, serif" fontSize={n.core ? 14 : 12}
                        fill="#1a140a" fontWeight="600">
                    {n.label.split(' ').slice(0, 2).join(' ')}
                  </text>
                )}
                {n.r >= 30 && (
                  <text x={cx} y={cy+12} textAnchor="middle"
                        fontFamily="EB Garamond, serif" fontSize={n.core ? 13 : 11}
                        fill="#1a140a">
                    {n.label.split(' ').slice(2).join(' ')}
                  </text>
                )}
                {/* External label */}
                <text x={cx} y={cy + n.r + 18} textAnchor="middle"
                      fontFamily="EB Garamond, serif" fontSize="13"
                      fill={hover === n.id ? '#E8C547' : '#A89878'}>
                  {n.r < 30 ? n.label : ''}
                </text>
                {n.sub && (
                  <text x={cx} y={cy + n.r + 32} textAnchor="middle"
                        fontFamily="Inter, sans-serif" fontSize="9"
                        letterSpacing="1" fill="#6B6359">
                    {n.sub.toUpperCase()}
                  </text>
                )}
              </g>
            );
          })}
        </svg>

        <div className="weaver-controls">
          <div className="zoom-pill">
            <span>ZOOM</span>
            <Icon name="minus" size={12}/>
            <input type="range" min="0" max="100" defaultValue="50"
                   style={{width:140, accentColor:'#D4AF37'}}/>
            <Icon name="plus" size={12}/>
          </div>
          <div className="density-pill">
            <span>NODE DENSITY</span>
            <input type="range" min="0" max="100" defaultValue="60"
                   style={{width:100, accentColor:'#D4AF37'}}/>
            <span style={{marginLeft:8, color:'var(--mythos-gold-deep)'}}>LAYOUT</span>
            <Icon name="graph" size={12}/>
            <Icon name="corkboard" size={12}/>
          </div>
        </div>
      </div>
    </div>
  );
}

import React from 'react';
import { Icon } from '../components/icons.jsx';

// Mythos — Corkboard (chapters by Act)

const ACTS = [
  {
    title: 'Act I · The Awakening', roman: 'I',
    chapters: [
      { title:'Chapter 1 — Whispers in the Market', syn:'Elara arrives in Oakhaven and senses the first stirrings of the Dusk War. A chance encounter with a hooded stranger.', status:'done', who:'Elena P.', due:'Oct 02' },
      { title:'Chapter 2 — The Sunstone', syn:'Lirael recovers the blade from the Citadel of Ash. The Citadel\u2019s libraries reveal a name she did not expect.', status:'done', who:'Elena P.', due:'Oct 18' },
      { title:'Chapter 3 — The Labyrinth', syn:'Elara and Kael must navigate the maze of illusions to find the Oracle\u2019s chamber. Danger lurks in every shadow.', status:'progress', who:'Elena P.', due:'Nov 15' },
    ]
  },
  {
    title: 'Act II · The Trials', roman: 'II',
    chapters: [
      { title:'Chapter 4 — Trial by Fire', syn:'The party crosses the Embered Wastes; Brom\u2019s loyalty is tested by a vision he cannot speak aloud.', status:'progress', who:'Elena P.', due:'Nov 28' },
      { title:'Chapter 5 — The Serpent\u2019s Lair', syn:'A descent into the catacombs beneath Eldoria. The Shard reveals its first true voice.', status:'draft', who:'Elena P.', due:'Dec 10' },
      { title:'Chapter 6 — The Oath Breaks', syn:'Lirael is forced to choose between the Sworn Oath and the safety of those she has come to love.', status:'draft', who:'Elena P.', due:'Dec 22' },
    ]
  },
  {
    title: 'Act III · The Reckoning', roman: 'III',
    chapters: [
      { title:'Chapter 7 — The Reckoning', syn:'The party crosses into the Nightwood and discovers what the moon-curse has wrought.', status:'draft', who:'Unassigned', due:'Jan 12' },
      { title:'Chapter 8 — The Hidden Map', syn:'A long-forgotten cartography reveals the location of the Treaty Broken.', status:'outline', who:'Unassigned', due:'Jan 26' },
    ]
  },
  {
    title: 'Act IV · Ascension', roman: 'IV',
    chapters: [
      { title:'Chapter 9 — Ascension', syn:'The final climb. The Ledger closes. A door opens.', status:'outline', who:'Unassigned', due:'Feb 14' },
    ]
  },
];

function ChapterCard({ c }) {
  const statusClass = c.status === 'done' ? 'done' : c.status === 'progress' ? 'progress' : 'draft';
  const statusText = c.status === 'done' ? 'Complete' : c.status === 'progress' ? 'In Progress' : c.status === 'outline' ? 'Outline' : 'Draft';
  return (
    <div className="chapter-card">
      <div className="pin"/>
      <Icon name="corner" className="card-corner tl"/>
      <Icon name="corner" className="card-corner tr"/>
      <Icon name="corner" className="card-corner bl"/>
      <Icon name="corner" className="card-corner br"/>

      <h3>{c.title}</h3>
      <div className="syn-label">Synopsis</div>
      <div className="syn">{c.syn}</div>
      <div className="meta">
        <span><span className="k">Status</span><span className={'v ' + statusClass}>{statusText}</span></span>
        <span><span className="k">Scribe</span><span className="v">{c.who}</span></span>
        <span><span className="k">Due</span><span className="v">{c.due}</span></span>
      </div>
    </div>
  );
}

export function CorkboardScreen({ project }) {
  return (
    <div className="corkboard">
      <div className="corkboard-bar">
        <span className="title">{project || 'The Obsidian Scepter'}</span>
        <span className="sub">Project Corkboard · 9 chapters across 4 acts</span>
        <span className="right">
          <button className="btn ghost"><Icon name="filter" size={12}/> Filter</button>
          <button className="btn gold"><Icon name="plus" size={12}/> Add Chapter</button>
        </span>
      </div>

      <div className="corkboard-acts">
        {ACTS.map(act => (
          <div key={act.title} className="act-col">
            <div className="act-head">
              <span className="roman">{act.roman}</span>
              <span>{act.title.split('·')[1]}</span>
            </div>
            <div className="act-cards">
              {act.chapters.map(c => <ChapterCard key={c.title} c={c}/>)}
              <div className="add-card"><Icon name="plus" size={11}/> Add</div>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}

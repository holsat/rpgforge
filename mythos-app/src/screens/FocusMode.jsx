import React from 'react';
import { Icon, Ember } from '../components/icons.jsx';

// Mythos — Lumina Focus Mode (glassmorphic, blue, dreamy)

export function FocusMode({ onLeave }) {
  const [text, setText] = React.useState(`The morning light filtered softly through the ancient pines, casting long, fractured shadows across the mossy floor of Aethelwood. Elara paused, the weight of the silver amulet cool against her chest. This was not the woods of her childhood; the air hummed with an unfamiliar, vibrant energy, a silent whisper from the forgotten realm lay just beyond the veil. The stillness was profound, broken only by the rustle of leaves and the faint, melody of a distant, unseen stream. She focused on the words forming in her mind, the narrative flowing like water through the glass…`);

  return (
    <div className="lumina">
      <div className="lumina-bar">
        <button className="lumina-btn" onClick={onLeave}>
          <Icon name="logout" size={12}/> Leave Focus
        </button>
        <button className="lumina-btn gold-accent">
          <Ember size={12}/> AI Assistance
        </button>
      </div>

      <div className="lumina-stage">
        <div className="lumina-card">
          <h1>CHAPTER VII · THE ECHO OF AETHEL</h1>
          <div
            contentEditable
            suppressContentEditableWarning
            onInput={e => setText(e.currentTarget.textContent)}
            style={{outline:'none', whiteSpace:'pre-wrap'}}
          >
            <p>{text}</p>
          </div>
        </div>
      </div>

      <div className="lumina-foot">{text.split(/\s+/).filter(Boolean).length} words · ⌘⇧F to exit</div>
    </div>
  );
}

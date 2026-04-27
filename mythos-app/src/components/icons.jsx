// Mythos — shared icons (gold linework, 1.5px stroke)
import React from 'react';

export const Icon = ({ name, size = 18, color = 'currentColor', strokeWidth = 1.5, className }) => {
  const props = {
    width: size, height: size, viewBox: '0 0 24 24',
    fill: 'none', stroke: color, strokeWidth, strokeLinecap: 'round', strokeLinejoin: 'round',
    className,
  };
  switch (name) {
    case 'compass':
      return <svg {...props}><circle cx="12" cy="12" r="9"/><polygon points="16.24 7.76 14.12 14.12 7.76 16.24 9.88 9.88 16.24 7.76"/></svg>;
    case 'feather':
      return <svg {...props}><path d="M20.24 12.24a6 6 0 0 0-8.49-8.49L5 10.5V19h8.5z"/><line x1="16" y1="8" x2="2" y2="22"/><line x1="17.5" y1="15" x2="9" y2="15"/></svg>;
    case 'book':
      return <svg {...props}><path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20"/><path d="M6.5 2H20v20H6.5A2.5 2.5 0 0 1 4 19.5v-15A2.5 2.5 0 0 1 6.5 2z"/></svg>;
    case 'users':
      return <svg {...props}><path d="M17 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M23 21v-2a4 4 0 0 0-3-3.87"/><path d="M16 3.13a4 4 0 0 1 0 7.75"/></svg>;
    case 'map':
      return <svg {...props}><polygon points="1 6 1 22 8 18 16 22 23 18 23 2 16 6 8 2 1 6"/><line x1="8" y1="2" x2="8" y2="18"/><line x1="16" y1="6" x2="16" y2="22"/></svg>;
    case 'timeline':
      return <svg {...props}><line x1="3" y1="12" x2="21" y2="12"/><circle cx="7" cy="12" r="2"/><circle cx="17" cy="12" r="2"/><line x1="7" y1="6" x2="7" y2="9"/><line x1="17" y1="15" x2="17" y2="18"/></svg>;
    case 'graph':
      return <svg {...props}><circle cx="6" cy="6" r="2.5"/><circle cx="18" cy="6" r="2.5"/><circle cx="12" cy="14" r="3"/><circle cx="6" cy="20" r="2"/><circle cx="18" cy="20" r="2"/><line x1="8" y1="7" x2="10.5" y2="12"/><line x1="16" y1="7" x2="13.5" y2="12"/><line x1="10.5" y1="16" x2="7.5" y2="18.5"/><line x1="13.5" y1="16" x2="16.5" y2="18.5"/></svg>;
    case 'corkboard':
      return <svg {...props}><rect x="3" y="4" width="8" height="7" rx="1"/><rect x="13" y="4" width="8" height="7" rx="1"/><rect x="3" y="13" width="8" height="7" rx="1"/><rect x="13" y="13" width="8" height="7" rx="1"/></svg>;
    case 'settings':
      return <svg {...props}><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>;
    case 'help':
      return <svg {...props}><circle cx="12" cy="12" r="9"/><path d="M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>;
    case 'logout':
      return <svg {...props}><path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><polyline points="16 17 21 12 16 7"/><line x1="21" y1="12" x2="9" y2="12"/></svg>;
    case 'home': return <svg {...props}><path d="M3 12L12 4l9 8"/><path d="M5 10v10h14V10"/></svg>;
    case 'search': return <svg {...props}><circle cx="11" cy="11" r="7"/><line x1="21" y1="21" x2="16.5" y2="16.5"/></svg>;
    case 'plus': return <svg {...props}><line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/></svg>;
    case 'eye': return <svg {...props}><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/></svg>;
    case 'chevron-down': return <svg {...props}><polyline points="6 9 12 15 18 9"/></svg>;
    case 'chevron-right': return <svg {...props}><polyline points="9 6 15 12 9 18"/></svg>;
    case 'edit': return <svg {...props}><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/><path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"/></svg>;
    case 'history': return <svg {...props}><polyline points="3 12 3 4 11 4"/><path d="M3 12a9 9 0 1 0 3-6.7L3 8"/><polyline points="12 7 12 12 16 14"/></svg>;
    case 'share': return <svg {...props}><circle cx="18" cy="5" r="3"/><circle cx="6" cy="12" r="3"/><circle cx="18" cy="19" r="3"/><line x1="8.59" y1="13.51" x2="15.42" y2="17.49"/><line x1="15.41" y1="6.51" x2="8.59" y2="10.49"/></svg>;
    case 'export': return <svg {...props}><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>;
    case 'sigil-tree':
      return <svg {...props}><circle cx="12" cy="6" r="2"/><path d="M12 8v8"/><path d="M12 12L8 16"/><path d="M12 12l4 4"/><circle cx="8" cy="17" r="1.5"/><circle cx="16" cy="17" r="1.5"/></svg>;
    case 'sigil-eye':
      return <svg {...props}><path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="2"/><path d="M12 2v3M12 19v3"/></svg>;
    case 'sigil-blade':
      return <svg {...props}><path d="M14 2L4 12l3 3 10-10z"/><path d="M19 19l-3-3 4-4 3 3z"/><line x1="15" y1="15" x2="19" y2="19"/></svg>;
    case 'sigil-flame':
      return <svg {...props}><path d="M12 2c1 4 4 5 4 9a4 4 0 0 1-8 0c0-2 1-3 1-5"/><path d="M12 22a4 4 0 0 1-4-4c0-2 2-3 2-5 1 2 3 2 3 4"/></svg>;
    case 'sigil-rune':
      return <svg {...props}><path d="M6 3v18M18 3v18"/><path d="M6 8l12 8"/><path d="M6 12h12"/></svg>;
    case 'sigil-crown':
      return <svg {...props}><path d="M3 17l2-9 4 5 3-7 3 7 4-5 2 9z"/><path d="M3 19h18"/></svg>;
    case 'lantern':
      return <svg {...props}><path d="M9 3h6"/><path d="M10 3v3h4V3"/><path d="M7 6h10l-1 13a2 2 0 0 1-2 2h-4a2 2 0 0 1-2-2z"/><line x1="12" y1="10" x2="12" y2="17"/></svg>;
    case 'pin':
      return <svg {...props}><path d="M12 2C8 2 5 5 5 9c0 5.5 7 13 7 13s7-7.5 7-13c0-4-3-7-7-7z"/><circle cx="12" cy="9" r="2.5"/></svg>;
    case 'send':
      return <svg {...props}><line x1="22" y1="2" x2="11" y2="13"/><polygon points="22 2 15 22 11 13 2 9 22 2"/></svg>;
    case 'corner':
      return <svg {...props}><path d="M3 3h8M3 3v8M3 3l5 5"/></svg>;
    case 'sparkle':
      return <svg {...props}><path d="M12 3l1.5 5.5L19 10l-5.5 1.5L12 17l-1.5-5.5L5 10l5.5-1.5z"/></svg>;
    case 'reset':
      return <svg {...props}><polyline points="1 4 1 10 7 10"/><path d="M3.51 15a9 9 0 1 0 2.13-9.36L1 10"/></svg>;
    case 'minus': return <svg {...props}><line x1="5" y1="12" x2="19" y2="12"/></svg>;
    case 'expand': return <svg {...props}><polyline points="15 3 21 3 21 9"/><polyline points="9 21 3 21 3 15"/><line x1="21" y1="3" x2="14" y2="10"/><line x1="3" y1="21" x2="10" y2="14"/></svg>;
    case 'filter': return <svg {...props}><polygon points="22 3 2 3 10 12.46 10 19 14 21 14 12.46 22 3"/></svg>;
    default: return <svg {...props}><circle cx="12" cy="12" r="9"/></svg>;
  }
};

export const Compass = ({ size = 22 }) => (
  <svg width={size} height={size} viewBox="0 0 24 24" fill="none">
    <circle cx="12" cy="12" r="10" stroke="currentColor" strokeWidth="1.2" opacity="0.4"/>
    <circle cx="12" cy="12" r="7" stroke="currentColor" strokeWidth="0.8" opacity="0.3"/>
    <polygon points="12,3 14,12 12,21 10,12" fill="currentColor" opacity="0.85"/>
    <polygon points="3,12 12,10 21,12 12,14" fill="currentColor" opacity="0.6"/>
    <circle cx="12" cy="12" r="1.5" fill="currentColor"/>
  </svg>
);

export const Ember = ({ size = 18 }) => (
  <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.4" strokeLinecap="round" strokeLinejoin="round">
    <path d="M12 3c1 4 5 5 5 10a5 5 0 0 1-10 0c0-3 2-4 2-7" />
    <path d="M12 14c1 1 2 2 2 4a2 2 0 0 1-4 0c0-1 1-2 2-2" fill="currentColor" fillOpacity="0.3"/>
  </svg>
);

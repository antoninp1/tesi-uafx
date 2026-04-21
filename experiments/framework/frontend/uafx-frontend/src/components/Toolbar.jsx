import React from 'react';

export default function Toolbar({ onRerun }) {
  return (
    <div className="toolbar">
      <button className="toolbar-button" onClick={onRerun}>
        ↻ Re-run Discovery
      </button>
    </div>
  );
}

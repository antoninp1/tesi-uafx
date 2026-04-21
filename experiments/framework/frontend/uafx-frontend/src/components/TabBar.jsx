import React from 'react';

export default function TabBar({ active, onChange }) {
  return (
    <div className="tab-bar">
      <button
        className={`tab ${active === 'physical' ? 'active' : ''}`}
        onClick={() => onChange('physical')}
      >
        Physical Topology
      </button>
      <button
        className={`tab ${active === 'logical' ? 'active' : ''}`}
        onClick={() => onChange('logical')}
      >
        Logical View
      </button>
    </div>
  );
}

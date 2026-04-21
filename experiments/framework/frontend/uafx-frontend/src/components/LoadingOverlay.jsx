import React from 'react';

export default function LoadingOverlay({ message }) {
  return (
    <div className="loading-overlay">
      <div className="loading-card">
        <div className="spinner" />
        <p>{message || 'Loading...'}</p>
      </div>
    </div>
  );
}

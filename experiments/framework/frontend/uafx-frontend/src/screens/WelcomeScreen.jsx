import React from 'react';

export default function WelcomeScreen({ onStart, errorMessage }) {
  return (
    <div className="welcome-screen">
      <div className="welcome-card">
        <h1>UAFX Topology Discovery</h1>
        <p className="subtitle">
          OPC UA FX discovery client for TSN-enabled industrial networks
        </p>
        <button className="primary-button" onClick={onStart}>
          Start Discovery
        </button>
        {errorMessage && (
          <div className="error-banner">
            <strong>Error:</strong> {errorMessage}
          </div>
        )}
        <p className="hint">
          You will be asked to provide the GDS (Global Discovery Server) address.
        </p>
      </div>
    </div>
  );
}

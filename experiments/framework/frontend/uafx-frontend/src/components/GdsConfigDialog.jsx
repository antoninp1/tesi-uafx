import React, { useState } from 'react';

export default function GdsConfigDialog({ onCancel, onConfirm }) {
  const [host, setHost] = useState('192.168.17.73');
  const [port, setPort] = useState('4840');
  const [validationError, setValidationError] = useState('');

  const handleSubmit = (e) => {
    e.preventDefault();

    const trimmedHost = host.trim();
    if (!trimmedHost) {
      setValidationError('Host cannot be empty');
      return;
    }

    const portNum = Number(port);
    if (!Number.isInteger(portNum) || portNum <= 0 || portNum > 65535) {
      setValidationError('Port must be an integer between 1 and 65535');
      return;
    }

    setValidationError('');
    onConfirm({ gdsHost: trimmedHost, gdsPort: portNum });
  };

  return (
    <div className="modal-overlay" onClick={onCancel}>
      <div className="modal-card" onClick={(e) => e.stopPropagation()}>
        <h2>Connect to GDS</h2>
        <p className="modal-description">
          Enter the address of the Global Discovery Server that the backend
          should query for UAFX servers.
        </p>

        <form onSubmit={handleSubmit}>
          <div className="form-field">
            <label htmlFor="gds-host">GDS IP / Hostname</label>
            <input
              id="gds-host"
              type="text"
              value={host}
              onChange={(e) => setHost(e.target.value)}
              placeholder="e.g. 192.168.17.73"
              autoFocus
            />
          </div>

          <div className="form-field">
            <label htmlFor="gds-port">Port</label>
            <input
              id="gds-port"
              type="number"
              value={port}
              onChange={(e) => setPort(e.target.value)}
              placeholder="4840"
              min="1"
              max="65535"
            />
          </div>

          {validationError && (
            <div className="validation-error">{validationError}</div>
          )}

          <div className="modal-actions">
            <button type="button" className="secondary-button" onClick={onCancel}>
              Cancel
            </button>
            <button type="submit" className="primary-button">
              Start Discovery
            </button>
          </div>
        </form>
      </div>
    </div>
  );
}

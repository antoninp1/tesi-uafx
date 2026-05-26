import React, { useState } from 'react';

export default function ConnectionDialog({ draft, onConfirm, onCancel }) {
  const { sourceNode, targetNode, sourceVar, targetVar } = draft;

  const [publishingInterval,    setPublishingInterval]    = useState('100');
  const [messageReceiveTimeout, setMessageReceiveTimeout] = useState('1000');
  const [address,               setAddress]               = useState('opc.udp://224.0.0.22:4840');
  const [qosCategory,           setQosCategory]           = useState('BEST_EFFORT');
  const [validationError,       setValidationError]       = useState('');

  const handleSubmit = (e) => {
    e.preventDefault();

    const interval = Number(publishingInterval);
    if (!interval || interval <= 0) {
      setValidationError('Publishing interval must be a positive number.');
      return;
    }

    const timeout = Number(messageReceiveTimeout);
    if (!timeout || timeout <= 0) {
      setValidationError('Message receive timeout must be a positive number.');
      return;
    }

    if (!address.trim().startsWith('opc.udp://')) {
      setValidationError('Address must start with opc.udp://');
      return;
    }

    setValidationError('');
    onConfirm({ publishingInterval: interval, messageReceiveTimeout: timeout,
                address: address.trim(), qosCategory });
  };

  return (
    <div className="modal-overlay" onClick={onCancel}>
      <div className="modal-card" onClick={(e) => e.stopPropagation()}>
        <h2>Establish Connection</h2>

        {/* Riepilogo sola lettura */}
        <div className="connection-summary">
          <div className="connection-summary-row">
            <span className="summary-label">Publisher</span>
            <span className="summary-value">
              {sourceNode.data.label}
              <span className="summary-sub">
                {sourceNode.data.parentDeviceName} / {sourceNode.data.parentAcName}
              </span>
            </span>
            <span className="summary-var">↑ {sourceVar.name}
              {sourceVar.units ? ` [${sourceVar.units}]` : ''}
            </span>
          </div>
          <div className="connection-summary-arrow">↓</div>
          <div className="connection-summary-row">
            <span className="summary-label">Subscriber</span>
            <span className="summary-value">
              {targetNode.data.label}
              <span className="summary-sub">
                {targetNode.data.parentDeviceName} / {targetNode.data.parentAcName}
              </span>
            </span>
            <span className="summary-var">↓ {targetVar.name}
              {targetVar.units ? ` [${targetVar.units}]` : ''}
            </span>
          </div>
        </div>

        <hr className="modal-divider" />

        <form onSubmit={handleSubmit}>
          <div className="form-field">
            <label htmlFor="publishing-interval">Publishing Interval (ms)</label>
            <input
              id="publishing-interval"
              type="number"
              value={publishingInterval}
              onChange={(e) => setPublishingInterval(e.target.value)}
              min="1"
              autoFocus
            />
          </div>

          <div className="form-field">
            <label htmlFor="receive-timeout">Message Receive Timeout (ms)</label>
            <input
              id="receive-timeout"
              type="number"
              value={messageReceiveTimeout}
              onChange={(e) => setMessageReceiveTimeout(e.target.value)}
              min="1"
            />
          </div>

          <div className="form-field">
            <label htmlFor="address">Multicast Address</label>
            <input
              id="address"
              type="text"
              value={address}
              onChange={(e) => setAddress(e.target.value)}
              placeholder="opc.udp://224.0.0.22:4840"
            />
          </div>

          <div className="form-field">
            <label htmlFor="qos">QoS Category</label>
            <select
              id="qos"
              value={qosCategory}
              onChange={(e) => setQosCategory(e.target.value)}
            >
              <option value="BEST_EFFORT">Best Effort</option>
              <option value="PRIORITY">Priority</option>
            </select>
          </div>

          {validationError && (
            <div className="validation-error">{validationError}</div>
          )}

          <div className="modal-actions">
            <button type="button" className="secondary-button" onClick={onCancel}>
              Cancel
            </button>
            <button type="submit" className="primary-button">
              Confirm
            </button>
          </div>
        </form>
      </div>
    </div>
  );
}
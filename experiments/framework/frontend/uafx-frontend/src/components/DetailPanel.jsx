import React from 'react';

export default function DetailPanel({ node, viewMode, onClose }) {
  const data = node.data?.raw;

  return (
    <aside className="detail-panel">
      <header className="detail-header">
        <h3>{node.data?.label || 'Node'}</h3>
        <button className="icon-button" onClick={onClose} title="Close">
          ×
        </button>
      </header>

      <div className="detail-content">
        {viewMode === 'physical' && data && renderPhysicalDetail(data)}
        {viewMode === 'logical' && data && renderLogicalDetail(data)}
      </div>
    </aside>
  );
}

function renderPhysicalDetail(node) {
  return (
    <>
      <Section title="Identity">
        <Field label="ChassisId" value={node.id} />
        <Field label="Type" value={node.type} />
        <Field label="Mgmt Address" value={node.mgmtAddress} />
        <Field label="Reachable" value={node.reachable ? 'yes' : 'no'} />
        {node.applicationName && (
          <Field label="Application" value={node.applicationName} />
        )}
        {node.endpointUrl && (
          <Field label="Endpoint" value={node.endpointUrl} />
        )}
      </Section>

      {node.automationComponents?.length > 0 && (
        <Section title="Automation Components">
          {node.automationComponents.map((ac, i) => (
            <div key={i} className="nested-item">
              <strong>{ac.name}</strong>
              <div className="subtle">
                {ac.assets?.length || 0} assets ·{' '}
                {ac.functionalEntities?.length || 0} FEs
              </div>
            </div>
          ))}
        </Section>
      )}

      {node.interfaces?.length > 0 && (
        <Section title={`Interfaces (${node.interfaces.length})`}>
          {node.interfaces.map((iface, i) => (
            <div key={i} className="nested-item">
              <strong>{iface.name}</strong>
              <div className="subtle">
                {iface.physAddress} · {iface.neighbors?.length || 0} neighbors
              </div>
            </div>
          ))}
        </Section>
      )}
    </>
  );
}

function renderLogicalDetail(fe) {
  return (
    <>
      <Section title="Functional Entity">
        <Field label="Name" value={fe.label} />
        <Field label="Parent Device" value={fe.parentDeviceName} />
        <Field label="Parent AC" value={fe.parentAcName} />
        {fe.authorAssignedIdentifier && (
          <Field label="Identifier" value={fe.authorAssignedIdentifier} />
        )}
        {fe.authorAssignedVersion && (
          <Field label="Version" value={fe.authorAssignedVersion} />
        )}
      </Section>

      {fe.outputs?.length > 0 && (
        <Section title={`Outputs (${fe.outputs.length})`}>
          {fe.outputs.map((o, i) => (
            <div key={i} className="nested-item">
              <strong>{o.name}</strong>
              {o.units && <span className="subtle"> [{o.units}]</span>}
            </div>
          ))}
        </Section>
      )}

      {fe.inputs?.length > 0 && (
        <Section title={`Inputs (${fe.inputs.length})`}>
          {fe.inputs.map((i, idx) => (
            <div key={idx} className="nested-item">
              <strong>{i.name}</strong>
              {i.units && <span className="subtle"> [{i.units}]</span>}
            </div>
          ))}
        </Section>
      )}
    </>
  );
}

function Section({ title, children }) {
  return (
    <div className="detail-section">
      <h4>{title}</h4>
      {children}
    </div>
  );
}

function Field({ label, value }) {
  return (
    <div className="detail-field">
      <span className="detail-label">{label}</span>
      <span className="detail-value">{value || '—'}</span>
    </div>
  );
}

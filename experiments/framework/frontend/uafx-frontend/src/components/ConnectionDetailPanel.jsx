import React, { useState, useEffect } from 'react';

export default function ConnectionDetailPanel({ edge, onClose }) {
  const d = edge.data || {};

  const [pubValue,  setPubValue]  = useState(null);
  const [subValue,  setSubValue]  = useState(null);
  const [polling,   setPolling]   = useState(false);

  // ── Polling dei valori live ──────────────────────────────
  useEffect(() => {
    if (!d.publisherVariableNodeId || !d.publisherEndpointUrl) return;

    setPolling(true);

    const fetchValues = async () => {
      try {
        const [pubRes, subRes] = await Promise.all([
          fetch(`http://localhost:8080/api/value?` +
            `endpointUrl=${encodeURIComponent(d.publisherEndpointUrl)}` +
            `&nodeId=${encodeURIComponent(d.publisherVariableNodeId)}`),
          fetch(`http://localhost:8080/api/value?` +
            `endpointUrl=${encodeURIComponent(d.subscriberEndpointUrl)}` +
            `&nodeId=${encodeURIComponent(d.subscriberVariableNodeId)}`),
        ]);
        const pubData = await pubRes.json();
        const subData = await subRes.json();
        if (pubData.value !== undefined) setPubValue(pubData.value);
        if (subData.value !== undefined) setSubValue(subData.value);
      } catch (_) {}
    };

    fetchValues();
    const interval = setInterval(fetchValues, 1000);

    return () => {
      clearInterval(interval);
      setPolling(false);
    };
  }, [
    d.publisherEndpointUrl,
    d.publisherVariableNodeId,
    d.subscriberEndpointUrl,
    d.subscriberVariableNodeId,
  ]);

  return (
    <aside className="detail-panel">
      <header className="detail-header">
        <h3>Connection Detail</h3>
        <button className="icon-button" onClick={onClose} title="Close">×</button>
      </header>

      <div className="detail-content">

        {/* ── PubSub Parameters ── */}
        <Section title="PubSub Parameters">
          <Field label="Publisher ID"      value={d.publisherId} />
          <Field label="Writer Group ID"   value={d.writerGroupId} />
          <Field label="DataSet Writer ID" value={d.dataSetWriterId} />
          <Field label="Multicast URL"     value={d.multicastUrl} />
          <Field label="Publishing Interval" value={d.publishingInterval ? `${d.publishingInterval} ms` : null} />
        </Section>

        {/* ── Publisher ── */}
        <Section title="Publisher">
          <Field label="Endpoint"    value={d.publisherEndpointUrl} />
          <Field label="Variable"    value={d.publisherVariableName} />
          <Field label="CE NodeId"   value={d.publisher?.nodeId} />
          <Field label="DSW Ref"     value={d.publisher?.dataSetWriterRef} />
          <Field label="Status"      value={d.publisher?.status} />
          <div className="live-value-row">
            <span className="detail-label">Live Value</span>
            <span className={`live-value ${polling ? 'live-value--active' : ''}`}>
              {pubValue !== null ? pubValue : '…'}
            </span>
          </div>
        </Section>

        {/* ── Subscriber ── */}
        <Section title="Subscriber">
          <Field label="Endpoint"    value={d.subscriberEndpointUrl} />
          <Field label="Variable"    value={d.subscriberVariableName} />
          <Field label="CE NodeId"   value={d.subscriber?.nodeId} />
          <Field label="DSR Ref"     value={d.subscriber?.dataSetReaderRef} />
          <Field label="Status"      value={d.subscriber?.status} />
          <div className="live-value-row">
            <span className="detail-label">Live Value</span>
            <span className={`live-value ${polling ? 'live-value--active' : ''}`}>
              {subValue !== null ? subValue : '…'}
            </span>
          </div>
        </Section>

      </div>
    </aside>
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
      <span className="detail-value">{value ?? '—'}</span>
    </div>
  );
}
import React from 'react';
import { Handle, Position } from 'reactflow';

// Nodo della vista logica: una FunctionalEntity.
// Gli INPUT (subscriber) sono in ALTO — ricevono dati.
// Gli OUTPUT (publisher) sono in BASSO — inviano dati.
// Ogni handle rappresenta una DataVariable nominata con la sua unita'.

export default function FunctionalEntityNode({ data }) {
  const raw = data.raw;
  const inputs = raw?.inputs || [];
  const outputs = raw?.outputs || [];

  return (
    <div className="node node-fe">
      {/* ────────── INPUT handles in alto ────────── */}
      {inputs.length > 0 && (
        <div className="fe-handles fe-handles-top">
          {inputs.map((input, idx) => {
            // Distribuisce gli handle orizzontalmente in percentuale
            const left = ((idx + 1) / (inputs.length + 1)) * 100;
            return (
              <React.Fragment key={`in-${idx}`}>
                <Handle
                  type="target"
                  position={Position.Top}
                  id={`input-${input.name}`}
                  style={{ left: `${left}%` }}
                />
                <div
                  className="fe-handle-label fe-handle-label-top"
                  style={{ left: `${left}%` }}
                  title={`${input.name}${input.units ? ' [' + input.units + ']' : ''}`}
                >
                  {input.name}
                </div>
              </React.Fragment>
            );
          })}
        </div>
      )}

      {/* ────────── Corpo del nodo ────────── */}
      <div className="node-header">
        <span className="node-icon">◆</span>
        <div>
          <div className="node-title">{data.label}</div>
          <div className="node-subtitle">
            {raw?.parentDeviceName || 'Functional Entity'}
          </div>
        </div>
      </div>

      {raw?.parentAcName && (
        <div className="node-meta">{raw.parentAcName}</div>
      )}

      <div className="fe-counts">
        <span className="fe-count-in">↓ {inputs.length} in</span>
        <span className="fe-count-out">↑ {outputs.length} out</span>
      </div>

      {/* ────────── OUTPUT handles in basso ────────── */}
      {outputs.length > 0 && (
        <div className="fe-handles fe-handles-bottom">
          {outputs.map((output, idx) => {
            const left = ((idx + 1) / (outputs.length + 1)) * 100;
            return (
              <React.Fragment key={`out-${idx}`}>
                <Handle
                  type="source"
                  position={Position.Bottom}
                  id={`output-${output.name}`}
                  style={{ left: `${left}%` }}
                />
                <div
                  className="fe-handle-label fe-handle-label-bottom"
                  style={{ left: `${left}%` }}
                  title={`${output.name}${output.units ? ' [' + output.units + ']' : ''}`}
                >
                  {output.name}
                </div>
              </React.Fragment>
            );
          })}
        </div>
      )}
    </div>
  );
}

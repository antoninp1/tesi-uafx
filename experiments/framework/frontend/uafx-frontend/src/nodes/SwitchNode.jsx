import React from 'react';
import { Handle, Position } from 'reactflow';

export default function SwitchNode({ data }) {
  const raw = data.raw;
  const interfaces = raw?.interfaces || [];

  return (
    <div className="node node-switch">
      <div className="node-header">
        <span className="node-icon">⇆</span>
        <div>
          <div className="node-title">{data.label}</div>
          <div className="node-subtitle">Switch</div>
        </div>
      </div>

      {raw?.mgmtAddress && (
        <div className="node-meta">{raw.mgmtAddress}</div>
      )}

      {interfaces.length > 0 && (
        <div className="node-ports node-ports-grid">
          {interfaces.map((iface) => (
            <div key={iface.name} className="node-port-chip">
              <Handle
                type="source"
                position={Position.Bottom}
                id={`port-${iface.name}`}
              />
              <Handle
                type="target"
                position={Position.Top}
                id={`port-${iface.name}-in`}
              />
              <span>{iface.name}</span>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

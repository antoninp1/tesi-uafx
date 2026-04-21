import React from 'react';
import { Handle, Position } from 'reactflow';

export default function UafxServerNode({ data }) {
  const raw = data.raw;
  const interfaces = raw?.interfaces || [];

  return (
    <div className="node node-uafx">
      <div className="node-header">
        <span className="node-icon">⚙</span>
        <div>
          <div className="node-title">{data.label}</div>
          <div className="node-subtitle">UAFX Server</div>
        </div>
      </div>

      {raw?.mgmtAddress && (
        <div className="node-meta">{raw.mgmtAddress}</div>
      )}

      {interfaces.length > 0 && (
        <div className="node-ports">
          {interfaces.map((iface) => (
            <div key={iface.name} className="node-port">
              <span>{iface.name}</span>
              <Handle
                type="source"
                position={Position.Right}
                id={`port-${iface.name}`}
              />
              <Handle
                type="target"
                position={Position.Right}
                id={`port-${iface.name}-in`}
              />
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

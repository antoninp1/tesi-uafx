import React from 'react';
import { Handle, Position } from 'reactflow';

export default function PhantomNode({ data }) {
  const raw = data.raw;
  return (
    <div className="node node-phantom">
      <Handle type="target" position={Position.Top} />
      <Handle type="source" position={Position.Bottom} />
      <div className="node-header">
        <span className="node-icon">?</span>
        <div>
          <div className="node-title">{data.label}</div>
          <div className="node-subtitle">Unknown / Not reachable</div>
        </div>
      </div>
      {raw?.mgmtAddress && (
        <div className="node-meta">{raw.mgmtAddress}</div>
      )}
    </div>
  );
}

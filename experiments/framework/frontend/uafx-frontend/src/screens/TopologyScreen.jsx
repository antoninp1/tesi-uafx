import React, { useState, useEffect, useCallback } from 'react';
import ReactFlow, {
  Background,
  Controls,
  MiniMap,
  ReactFlowProvider,
  useNodesState,
  useEdgesState,
} from 'reactflow';
import 'reactflow/dist/style.css';

import TabBar from '../components/TabBar.jsx';
import Toolbar from '../components/Toolbar.jsx';
import DetailPanel from '../components/DetailPanel.jsx';

import UafxServerNode from '../nodes/UafxServerNode.jsx';
import SwitchNode from '../nodes/SwitchNode.jsx';
import PhantomNode from '../nodes/PhantomNode.jsx';
import FunctionalEntityNode from '../nodes/FunctionalEntityNode.jsx';

import { transformPhysical, transformLogical } from '../utils/transform.js';

const nodeTypes = {
  uafx_server: UafxServerNode,
  switch: SwitchNode,
  phantom: PhantomNode,
  functional_entity: FunctionalEntityNode,
};

export default function TopologyScreen({ topology, onRerun }) {
  const [viewMode, setViewMode] = useState('physical');
  const [selectedNode, setSelectedNode] = useState(null);

  // Ricalcola nodes/edges quando cambia vista o topology
  const [nodes, setNodes, onNodesChange] = useNodesState([]);
const [edges, setEdges, onEdgesChange] = useEdgesState([]);

// Ricalcola il layout quando cambia vista o topology
useEffect(() => {
  const result = viewMode === 'physical'
    ? transformPhysical(topology.physical)
    : transformLogical(topology.logical);
  setNodes(result.nodes);
  setEdges(result.edges);
}, [viewMode, topology, setNodes, setEdges]);

  const handleNodeClick = useCallback((event, node) => {
    setSelectedNode(node);
  }, []);

  const handlePaneClick = useCallback(() => {
    setSelectedNode(null);
  }, []);

  const handleViewChange = (newMode) => {
    setViewMode(newMode);
    setSelectedNode(null);
  };

  return (
    <div className="topology-screen">
      <header className="app-header">
        <h1>UAFX Topology</h1>
        <div className="header-info">
          <span>Last scan: {topology.lastScan || 'unknown'}</span>
          <span>
            {viewMode === 'physical'
              ? `${topology.physical.nodes.length} devices · ${topology.physical.links.length} links`
              : `${topology.logical.nodes.length} functional entities`}
          </span>
        </div>
      </header>

      <TabBar active={viewMode} onChange={handleViewChange} />
      <Toolbar onRerun={onRerun} />

      <div className="graph-container">
        <ReactFlowProvider>
          <ReactFlow
            nodes={nodes}
            edges={edges}
            nodeTypes={nodeTypes}
            onNodesChange={onNodesChange}
            onEdgesChange={onEdgesChange}
            onNodeClick={handleNodeClick}
            onPaneClick={handlePaneClick}
            fitView
            minZoom={0.2}
            maxZoom={1.5}
          >
            <Background gap={16} color="#e0e0e0" />
            <Controls />
            <MiniMap
              nodeColor={(node) => {
                switch (node.type) {
                  case 'uafx_server': return '#3b82f6';
                  case 'switch': return '#10b981';
                  case 'phantom': return '#9ca3af';
                  case 'functional_entity': return '#8b5cf6';
                  default: return '#6b7280';
                }
              }}
            />
          </ReactFlow>
        </ReactFlowProvider>

        {selectedNode && (
          <DetailPanel
            node={selectedNode}
            viewMode={viewMode}
            onClose={() => setSelectedNode(null)}
          />
        )}
      </div>
    </div>
  );
}

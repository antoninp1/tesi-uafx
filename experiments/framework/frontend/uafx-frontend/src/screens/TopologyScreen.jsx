import React, { useState, useEffect, useCallback } from 'react';
import ReactFlow, {
  Background, Controls, MiniMap,
  ReactFlowProvider,
  useNodesState, useEdgesState,
} from 'reactflow';
import 'reactflow/dist/style.css';

import TabBar from '../components/TabBar.jsx';
import Toolbar from '../components/Toolbar.jsx';
import DetailPanel from '../components/DetailPanel.jsx';
import ConnectionDetailPanel from '../components/ConnectionDetailPanel.jsx';
import ConnectionDialog from '../components/ConnectionDialog.jsx';

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
  const [selectedEdge, setSelectedEdge] = useState(null);  
  const [connectionDraft, setConnectionDraft] = useState(null);
  const [connectionError, setConnectionError] = useState(null);

  const [nodes, setNodes, onNodesChange] = useNodesState([]);
  const [edges, setEdges, onEdgesChange] = useEdgesState([]);

  useEffect(() => {
    const result = viewMode === 'physical'
      ? transformPhysical(topology.physical)
      : transformLogical(topology.logical);
    setNodes(result.nodes);
    setEdges(result.edges);
  }, [viewMode, topology, setNodes, setEdges]);

  // ── Validazione e apertura dialog ─────────────────────────
  const onConnect = useCallback((params) => {
    if (viewMode !== 'logical') return;

    const sourceNode = nodes.find(n => n.id === params.source);
    const targetNode = nodes.find(n => n.id === params.target);
    if (!sourceNode || !targetNode) return;

    // Estrai il nome della variabile dall'handle id
    // sourceHandle es. "output-Temperature"
    // targetHandle es. "input-ReceivedTemperature"
    const outputName = params.sourceHandle?.replace('output-', '');
    const inputName  = params.targetHandle?.replace('input-', '');

    const sourceVar = sourceNode.data.outputs?.find(o => o.name === outputName);
    const targetVar = targetNode.data.inputs?.find(i  => i.name === inputName);

    if (!sourceVar || !targetVar) {
      setConnectionError('Could not resolve variables from handles.');
      return;
    }

    // Validazione direzione: sourceHandle deve essere output, targetHandle input
    if (!params.sourceHandle?.startsWith('output-') ||
        !params.targetHandle?.startsWith('input-')) {
      setConnectionError('Direction mismatch: connect an output to an input.');
      return;
    }

    // Validazione units
    if (sourceVar.units && targetVar.units &&
        sourceVar.units !== targetVar.units) {
      setConnectionError(
        `Incompatible variables: source is [${sourceVar.units}], ` +
        `target is [${targetVar.units}].`
      );
      return;
    }

    // Validazione tipo
    if (sourceVar.type && targetVar.type &&
        sourceVar.type !== targetVar.type) {
      setConnectionError(
        `Incompatible types: source is ${sourceVar.type}, ` +
        `target is ${targetVar.type}.`
      );
      return;
    }

    // Tutto ok: apri il dialog con il draft
    setConnectionDraft({
      sourceNode,
      targetNode,
      sourceVar,
      targetVar,
      sourceHandle: params.sourceHandle,
      targetHandle: params.targetHandle,
    });
  }, [nodes, viewMode]);

  const handleConnectionConfirm = useCallback(async (pubsubParams) => {
    const { sourceNode, targetNode, sourceVar, targetVar } = connectionDraft;

    const payload = {
      publisher: {
        endpointUrl:        sourceNode.data.endpointUrl,
        acNodeId:           sourceNode.data.acNodeId,
        acName:             sourceNode.data.parentAcName,
        feNodeId:           sourceNode.data.feNodeId,
        feName:             sourceNode.data.label,
        variableNodeId:     sourceVar.nodeId,
        variableName:       sourceVar.name,
      },
      subscriber: {
        endpointUrl:        targetNode.data.endpointUrl,
        acNodeId:           targetNode.data.acNodeId,
        acName:             targetNode.data.parentAcName,
        feNodeId:           targetNode.data.feNodeId,
        feName:             targetNode.data.label,
        variableNodeId:     targetVar.nodeId,
        variableName:       targetVar.name,
      },
      pubsub: {
        publishingInterval:     pubsubParams.publishingInterval,
        messageReceiveTimeout:  pubsubParams.messageReceiveTimeout,
        address:                pubsubParams.address,
        qosCategory:            pubsubParams.qosCategory,
      },
    };

    try {
      const res = await fetch('http://localhost:8080/api/connect', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload),
      });
      const data = await res.json();

      if (data.success) {
        setEdges(eds => [...eds, {
          id: `conn-${Date.now()}`,
          source: connectionDraft.sourceNode.id,
          target: connectionDraft.targetNode.id,
          sourceHandle: connectionDraft.sourceHandle,
          targetHandle: connectionDraft.targetHandle,
          animated: true,
          style: { stroke: '#22c55e', strokeWidth: 2 },
          data: {
            ...data.connection,
            publisherEndpointUrl:     connectionDraft.sourceNode.data.endpointUrl,
            subscriberEndpointUrl:    connectionDraft.targetNode.data.endpointUrl,
            publisherVariableNodeId:  connectionDraft.sourceVar.nodeId,
            subscriberVariableNodeId: connectionDraft.targetVar.nodeId,
            publisherVariableName:    connectionDraft.sourceVar.name,
            subscriberVariableName:   connectionDraft.targetVar.name,
          },
        }]);

      } else {
        setConnectionError(data.error || 'Connection failed.');
      }
    } catch (err) {
      setConnectionError('HTTP error: ' + err.message);
    }

    setConnectionDraft(null);
  }, [connectionDraft, setEdges]);

  const handleNodeClick = useCallback((event, node) => {
    setSelectedNode(node);
  }, []);

  const handleEdgeClick = useCallback((event, edge) => {   
  setSelectedEdge(edge);
  setSelectedNode(null);
  }, []);
  const handlePaneClick = useCallback(() => {               
  setSelectedNode(null);
  setSelectedEdge(null);
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
            onEdgeClick={handleEdgeClick}
            onPaneClick={handlePaneClick}
            onConnect={onConnect}
            fitView
            minZoom={0.2}
            maxZoom={1.5}
          >
            <Background gap={16} color="#e0e0e0" />
            <Controls />
            <MiniMap nodeColor={(node) => {
              switch (node.type) {
                case 'uafx_server':       return '#3b82f6';
                case 'switch':            return '#10b981';
                case 'phantom':           return '#9ca3af';
                case 'functional_entity': return '#8b5cf6';
                default:                  return '#6b7280';
              }
            }} />
          </ReactFlow>
        </ReactFlowProvider>

        {selectedNode && (
          <DetailPanel
            node={selectedNode}
            viewMode={viewMode}
            onClose={() => setSelectedNode(null)}
          />
        )}

        {selectedEdge && (
          <ConnectionDetailPanel
            edge={selectedEdge}
            onClose={() => setSelectedEdge(null)}
          />
        )}

        {connectionDraft && (
          <ConnectionDialog
            draft={connectionDraft}
            onConfirm={handleConnectionConfirm}
            onCancel={() => setConnectionDraft(null)}
          />
        )}

        {connectionError && (
          <div className="connection-error-toast">
            <span>{connectionError}</span>
            <button onClick={() => setConnectionError(null)}>✕</button>
          </div>
        )}
      </div>
    </div>
  );
}
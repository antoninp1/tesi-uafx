import { layoutGraph } from './layout.js';

// ============================================================
// Vista fisica: TopologyNode + TopologyLink → React Flow
// ============================================================
export function transformPhysical(physical) {
  if (!physical || !physical.nodes) return { nodes: [], edges: [] };

  const rfNodes = physical.nodes.map((node) => ({
    id: node.id,
    type: node.type, // 'uafx_server' | 'switch' | 'phantom'
    position: { x: 0, y: 0 }, // dagre calcolera' le coordinate
    data: {
      label: node.label || node.id,
      raw: node,
    },
  }));

  const rfEdges = (physical.links || []).map((link) => ({
    id: link.id,
    source: link.source,
    target: link.target,
    sourceHandle: `port-${link.sourcePort}`,
    targetHandle: `port-${link.targetPort}-in`,
    label: `${link.sourcePort} ↔ ${link.targetPort}`,
    type: 'default',
    animated: false,
    style: {
      stroke: link.confirmed ? '#10b981' : '#94a3b8',
      strokeWidth: 2,
      strokeDasharray: link.confirmed ? '0' : '5,5',
    },
    labelStyle: { fontSize: 10 },
  }));

  // Layout top-bottom per la vista fisica
  const laidOut = layoutGraph(rfNodes, rfEdges, {
    direction: 'TB',
    nodeWidth: 260,
    nodeHeight: 180,
    nodeSep: 80,
    rankSep: 120,
  });

  return { nodes: laidOut, edges: rfEdges };
}

// ============================================================
// Vista logica: FunctionalEntities flattened → React Flow
// ============================================================
export function transformLogical(logical) {
  if (!logical || !logical.nodes) return { nodes: [], edges: [] };

  const rfNodes = logical.nodes.map((fe) => ({
    id: fe.id,
    type: 'functional_entity',
    position: { x: 0, y: 0 },
    data: {
      label: fe.label,
      raw: fe,
    },
  }));

  const rfEdges = (logical.links || []).map((link) => ({
    id: link.id,
    source: link.publisherFE  || link.source,
    target: link.subscriberFE || link.target,
    sourceHandle: link.outputVariable || link.sourceHandle,
    targetHandle: link.inputVariable  || link.targetHandle,
    type: 'default',
    animated: true,
    style: { stroke: '#22c55e', strokeWidth: 2 },
  }));

  // Layout top-bottom per la vista logica (input in alto → output in basso)
  const laidOut = layoutGraph(rfNodes, rfEdges, {
    direction: 'TB',
    nodeWidth: 240,
    nodeHeight: 200,
    nodeSep: 100,
    rankSep: 140,
  });

  return { nodes: laidOut, edges: rfEdges };
}

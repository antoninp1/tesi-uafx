import dagre from 'dagre';

// Applica un layout automatico con dagre al grafo React Flow.
// Modifica le coordinate x,y di ogni nodo in base alla topologia.
export function layoutGraph(nodes, edges, options = {}) {
  const {
    direction = 'TB',      // Top-Bottom: gli switch centrali, device attorno
    nodeWidth = 250,
    nodeHeight = 180,
    nodeSep = 80,          // distanza orizzontale tra nodi allo stesso livello
    rankSep = 120,         // distanza verticale tra livelli
  } = options;

  const g = new dagre.graphlib.Graph();
  g.setDefaultEdgeLabel(() => ({}));
  g.setGraph({
    rankdir: direction,
    nodesep: nodeSep,
    ranksep: rankSep,
    marginx: 40,
    marginy: 40,
  });

  // Registra i nodi con le loro dimensioni (dagre ne ha bisogno per il layout)
  nodes.forEach((node) => {
    g.setNode(node.id, {
      width: nodeWidth,
      height: nodeHeight,
    });
  });

  // Registra gli edges
  edges.forEach((edge) => {
    g.setEdge(edge.source, edge.target);
  });

  // Calcola il layout
  dagre.layout(g);

  // Applica le coordinate calcolate ai nodi React Flow
  // Dagre restituisce il centro del nodo, React Flow usa l'angolo top-left
  return nodes.map((node) => {
    const pos = g.node(node.id);
    if (!pos) return node;
    return {
      ...node,
      position: {
        x: pos.x - nodeWidth / 2,
        y: pos.y - nodeHeight / 2,
      },
    };
  });
}

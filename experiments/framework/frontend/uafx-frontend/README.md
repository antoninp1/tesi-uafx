# UAFX Frontend

Frontend React per la visualizzazione della topologia OPC UA FX, con due viste:
- **Physical**: dispositivi e connessioni di rete (server UAFX, switch, phantom node)
- **Logical**: FunctionalEntities con handle di input (in alto) e output (in basso) per le connessioni PubSub

## Setup

```bash
npm install
npm run dev
```

L'app si apre su http://localhost:5173 e fa proxy delle chiamate `/api/*` verso il backend C su `http://localhost:8080`. Assicurati che il backend (`./discovery_client`) sia in esecuzione.

## Build di produzione

```bash
npm run build
npm run preview
```

## Struttura

```
src/
├── App.jsx                           State machine principale
├── api/client.js                     Wrapper fetch per le chiamate al backend
├── screens/
│   ├── WelcomeScreen.jsx             Schermata iniziale con bottone Start
│   └── TopologyScreen.jsx            Canvas React Flow + tab + detail panel
├── components/
│   ├── GdsConfigDialog.jsx           Modale per inserire IP/porta GDS
│   ├── LoadingOverlay.jsx            Spinner durante la discovery
│   ├── TabBar.jsx                    Switch Physical/Logical
│   ├── Toolbar.jsx                   Bottone re-run discovery
│   └── DetailPanel.jsx               Sidebar destra con dettagli del nodo
├── nodes/                            Custom node React Flow
│   ├── UafxServerNode.jsx
│   ├── SwitchNode.jsx
│   ├── PhantomNode.jsx
│   └── FunctionalEntityNode.jsx      INPUT in alto, OUTPUT in basso
├── utils/
│   ├── transform.js                  JSON backend → nodes/edges React Flow
│   └── layout.js                     Wrapper dagre per auto-layout
└── styles/
    └── index.css                     Stili globali
```

## Flusso utente

1. Click su "Start Discovery" → modale per inserire IP/porta del GDS
2. Conferma → `POST /api/discovery/run` al backend con `{gdsHost, gdsPort}`
3. Spinner finché il backend risponde
4. Visualizzazione del grafo, possibilità di switchare tra le due viste
5. Click su un nodo → DetailPanel con tutti i metadati

import React, { useState } from 'react';
import WelcomeScreen from './screens/WelcomeScreen.jsx';
import TopologyScreen from './screens/TopologyScreen.jsx';
import GdsConfigDialog from './components/GdsConfigDialog.jsx';
import LoadingOverlay from './components/LoadingOverlay.jsx';
import { runDiscovery } from './api/client.js';

// Stati possibili dell'app:
//   'welcome'     → schermata iniziale con bottone "Start Discovery"
//   'configuring' → dialog aperto per inserire IP/porta del GDS
//   'discovering' → discovery in corso (spinner)
//   'ready'       → topologia caricata, mostra il grafo
//   'error'       → errore durante la discovery
export default function App() {
  const [appState, setAppState] = useState('welcome');
  const [topology, setTopology] = useState(null);
  const [errorMsg, setErrorMsg] = useState('');

  const handleStartClick = () => {
    setAppState('configuring');
  };

  const handleDialogCancel = () => {
    setAppState('welcome');
  };

  const handleDialogConfirm = async ({ gdsHost, gdsPort }) => {
    setAppState('discovering');
    setErrorMsg('');
    try {
      const data = await runDiscovery(gdsHost, gdsPort);
      setTopology(data);
      setAppState('ready');
    } catch (err) {
      console.error('[App] Discovery failed:', err);
      setErrorMsg(err.message || 'Discovery failed');
      setAppState('error');
    }
  };

  const handleRerun = async () => {
    // La vecchia topologia resta visibile finché non arriva quella nuova
    setAppState('configuring');
  };

  const handleErrorDismiss = () => {
    setAppState('welcome');
  };

  return (
    <>
      {appState === 'welcome' && <WelcomeScreen onStart={handleStartClick} />}

      {appState === 'error' && (
        <WelcomeScreen onStart={handleStartClick} errorMessage={errorMsg} />
      )}

      {(appState === 'configuring' || appState === 'discovering') && topology === null && (
        <WelcomeScreen onStart={handleStartClick} />
      )}

      {appState === 'ready' && topology && (
        <TopologyScreen topology={topology} onRerun={handleRerun} />
      )}

      {appState === 'configuring' && (
        <GdsConfigDialog
          onCancel={handleDialogCancel}
          onConfirm={handleDialogConfirm}
        />
      )}

      {appState === 'discovering' && (
        <LoadingOverlay message="Discovering topology... this may take a few seconds" />
      )}
    </>
  );
}

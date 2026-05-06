import React from 'react';
import { BrowserRouter, Routes, Route } from 'react-router-dom';

import MonitorView from './components/MonitorView';
import GameView from './components/GameView';

function App() {
  return (
    <BrowserRouter>
      <Routes>
        <Route path="/" element={<MonitorView />} />
        <Route path="/game" element={<GameView />} />
      </Routes>
    </BrowserRouter>
  );
}

export default App;

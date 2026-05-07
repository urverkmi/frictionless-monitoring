import React, { useRef, useEffect, useState } from 'react';
import { GAME_CONFIG } from '../gameConfig';

// Maps a world-frame point (metres) to canvas pixel coordinates.
// World: +x right, +y up, origin at table centre.
// Canvas: +x right, +y down, origin at top-left.
function worldToCanvas(world, canvasWidth, canvasHeight) {
  const px = canvasWidth / 2 + world.x * GAME_CONFIG.pixelsPerMetre;
  const py = canvasHeight / 2 - world.y * GAME_CONFIG.pixelsPerMetre;
  return { x: px, y: py };
}

function GameCanvas({ puckPosition, target, hitFlashUntil }) {
  const canvasRef = useRef(null);
  const snoopyImgRef = useRef(null);
  const [snoopyLoaded, setSnoopyLoaded] = useState(false);

  useEffect(() => {
    const img = new Image();
    img.onload = () => setSnoopyLoaded(true);
    img.src = `${process.env.PUBLIC_URL}/snoopy_PNG27.png`;
    snoopyImgRef.current = img;
  }, []);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    const width = canvas.width;
    const height = canvas.height;

    // Background
    ctx.fillStyle = '#0f172a';
    ctx.fillRect(0, 0, width, height);

    // Subtle grid (every 0.1 m)
    ctx.strokeStyle = '#1e293b';
    ctx.lineWidth = 1;
    const gridStep = 0.1 * GAME_CONFIG.pixelsPerMetre;
    for (let x = (width / 2) % gridStep; x < width; x += gridStep) {
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, height);
      ctx.stroke();
    }
    for (let y = (height / 2) % gridStep; y < height; y += gridStep) {
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(width, y);
      ctx.stroke();
    }

    // Centre axes
    ctx.strokeStyle = '#334155';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(width / 2, 0);
    ctx.lineTo(width / 2, height);
    ctx.moveTo(0, height / 2);
    ctx.lineTo(width, height / 2);
    ctx.stroke();

    // Table outline (calibration rectangle)
    const tl = worldToCanvas(
      { x: -GAME_CONFIG.tableHalfWidth, y: GAME_CONFIG.tableHalfHeight },
      width, height,
    );
    const br = worldToCanvas(
      { x: GAME_CONFIG.tableHalfWidth, y: -GAME_CONFIG.tableHalfHeight },
      width, height,
    );
    ctx.strokeStyle = '#475569';
    ctx.lineWidth = 3;
    ctx.strokeRect(tl.x, tl.y, br.x - tl.x, br.y - tl.y);

    // Target
    if (target) {
      const tc = worldToCanvas(target, width, height);
      const rPx = target.radius * GAME_CONFIG.pixelsPerMetre;
      const flashing = hitFlashUntil && Date.now() < hitFlashUntil;
      ctx.fillStyle = flashing ? '#22c55e55' : '#ef444433';
      ctx.strokeStyle = flashing ? '#22c55e' : '#ef4444';
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.arc(tc.x, tc.y, rPx, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();

      // Bullseye dot
      ctx.fillStyle = flashing ? '#22c55e' : '#ef4444';
      ctx.beginPath();
      ctx.arc(tc.x, tc.y, 3, 0, Math.PI * 2);
      ctx.fill();
    }

    // Puck (Snoopy head — sized at 56 px on the longer side, preserving aspect ratio)
    if (puckPosition) {
      const pc = worldToCanvas(puckPosition, width, height);
      const img = snoopyImgRef.current;
      const longSide = 56;
      if (img && img.complete && img.naturalWidth > 0) {
        const aspect = img.naturalWidth / img.naturalHeight;
        const drawW = aspect >= 1 ? longSide : longSide * aspect;
        const drawH = aspect >= 1 ? longSide / aspect : longSide;
        ctx.drawImage(img, pc.x - drawW / 2, pc.y - drawH / 2, drawW, drawH);
      }
    }
  }, [puckPosition, target, hitFlashUntil, snoopyLoaded]);

  // Canvas pixel size derives from the configured table size + scale, so the
  // table outline always fills the canvas with the right aspect ratio.
  const canvasW = Math.round(2 * GAME_CONFIG.tableHalfWidth * GAME_CONFIG.pixelsPerMetre + 100);
  const canvasH = Math.round(2 * GAME_CONFIG.tableHalfHeight * GAME_CONFIG.pixelsPerMetre + 100);

  return (
    <div style={{
      flex: 1,
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      padding: 24,
      borderRight: '2px solid #334155',
    }}>
      <div style={{
        backgroundColor: '#0f172a',
        borderRadius: 12,
        boxShadow: '0 25px 50px -12px rgba(0,0,0,0.5)',
        overflow: 'hidden',
      }}>
        <canvas
          ref={canvasRef}
          width={canvasW}
          height={canvasH}
          style={{ display: 'block', maxWidth: '100%', height: 'auto' }}
        />
      </div>
    </div>
  );
}

export default GameCanvas;

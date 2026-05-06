import React, { useRef, useEffect } from 'react';

function MainDisplay({ data }) {
  const canvasRef = useRef(null);
  const viewRef = useRef(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');

    // Use the actual pixel dimensions, not CSS dimensions
    const width = canvas.width;
    const height = canvas.height;

    const satellitePosition = data.satellitePosition || data.mainPosition;
    if (!satellitePosition || !data.endMassPosition) {
      return;
    }

    const satWorld = { x: satellitePosition.x, y: satellitePosition.y };
    const endWorld = { x: data.endMassPosition.x, y: data.endMassPosition.y };

    // Reset view when positions change significantly (or on first frame)
    const worldDistance = Math.hypot(
      endWorld.x - satWorld.x,
      endWorld.y - satWorld.y
    );

    // Guard: if the two objects are essentially the same point, skip rendering
    if (worldDistance < 1e-6 && !data.tetherLength) {
      return;
    }

    if (!viewRef.current) {
      const centerX = (satWorld.x + endWorld.x) / 2;
      const centerY = (satWorld.y + endWorld.y) / 2;

      // Use tether length if available, otherwise use actual distance
      // Multiply by 2.5 so both objects have comfortable margin
      const refSpan = Math.max(
        (data.tetherLength || worldDistance) * 2.5,
        worldDistance * 2.5, // always fit the actual gap
        0.1
      );

      const padding = 60;
      const scale = Math.min(
        (width - padding * 2) / refSpan,
        (height - padding * 2) / refSpan
      );

      viewRef.current = { centerX, centerY, scale };
    }

    const { centerX, centerY, scale } = viewRef.current;
    const originX = width / 2;
    const originY = height / 2;

    const worldToCanvas = (point) => ({
      x: originX + (point.x - centerX) * scale,
      y: originY - (point.y - centerY) * scale, // flip Y: world +Y is up
    });

    const sat = worldToCanvas(satWorld);
    const end = worldToCanvas(endWorld);

    // --- Clear ---
    ctx.fillStyle = '#0f172a';
    ctx.fillRect(0, 0, width, height);

    // --- Grid ---
    ctx.strokeStyle = '#1e293b';
    ctx.lineWidth = 1;
    const gridSize = 50;
    for (let x = 0; x < width; x += gridSize) {
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke();
    }
    for (let y = 0; y < height; y += gridSize) {
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke();
    }

    // --- Center axes ---
    ctx.strokeStyle = '#334155';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(width / 2, 0); ctx.lineTo(width / 2, height);
    ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2);
    ctx.stroke();

    // --- Debug: log canvas positions so you can verify ---
    // console.log('sat canvas:', sat, 'end canvas:', end, 'scale:', scale);

    // --- Tether ---
    ctx.strokeStyle = '#64748b';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(sat.x, sat.y);
    ctx.lineTo(end.x, end.y);
    ctx.stroke();

    // --- Satellite: large green circle ---
    const satelliteRadius = 16;
    ctx.beginPath();
    ctx.arc(sat.x, sat.y, satelliteRadius, 0, Math.PI * 2);
    ctx.fillStyle = '#22c55e';
    ctx.fill();
    ctx.strokeStyle = '#4ade80';
    ctx.lineWidth = 2;
    ctx.stroke();

    // Label
    ctx.fillStyle = '#4ade80';
    ctx.font = '12px monospace';
    ctx.fillText('SAT', sat.x + satelliteRadius + 4, sat.y + 4);

    // --- End mass: small yellow circle ---
    const endMassRadius = 8;
    ctx.beginPath();
    ctx.arc(end.x, end.y, endMassRadius, 0, Math.PI * 2);
    ctx.fillStyle = '#f59e0b';
    ctx.fill();
    ctx.strokeStyle = '#fbbf24';
    ctx.lineWidth = 2;
    ctx.stroke();

    // Label
    ctx.fillStyle = '#fbbf24';
    ctx.font = '12px monospace';
    ctx.fillText('END', end.x + endMassRadius + 4, end.y + 4);

  }, [data]);

  return (
    <div className="flex-1 flex items-center justify-center p-8 border-r-2 border-slate-700">
      <div className="w-full h-full bg-slate-900 rounded-xl shadow-2xl overflow-hidden">
        <canvas
          ref={canvasRef}
          width={800}
          height={600}
          style={{ width: '100%', height: '100%' }}
        />
      </div>
    </div>
  );
}

export default MainDisplay;
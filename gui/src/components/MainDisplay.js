import React, { useRef, useEffect } from 'react';

function MainDisplay({ data }) {
  const canvasRef = useRef(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    const width = canvas.width;
    const height = canvas.height;

    // Clear canvas
    ctx.fillStyle = '#0f172a';
    ctx.fillRect(0, 0, width, height);

    // Draw grid
    ctx.strokeStyle = '#1e293b';
    ctx.lineWidth = 1;
    const gridSize = 50;
    for (let x = 0; x < width; x += gridSize) {
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, height);
      ctx.stroke();
    }
    for (let y = 0; y < height; y += gridSize) {
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(width, y);
      ctx.stroke();
    }

    // Center axes
    ctx.strokeStyle = '#334155';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(width / 2, 0);
    ctx.lineTo(width / 2, height);
    ctx.moveTo(0, height / 2);
    ctx.lineTo(width, height / 2);
    ctx.stroke();

    // Transform to center origin
    ctx.save();
    ctx.translate(width / 2, height / 2);

    // Draw tether (line connecting main unit to end mass)
    ctx.strokeStyle = '#64748b';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(data.mainPosition.x, data.mainPosition.y);
    ctx.lineTo(data.endMassPosition.x, data.endMassPosition.y);
    ctx.stroke();

    // Draw main satellite unit (square)
    const halfSize = data.mainSize / 2;
    ctx.fillStyle = '#3b82f6';
    ctx.strokeStyle = '#60a5fa';
    ctx.lineWidth = 2;
    ctx.fillRect(
      data.mainPosition.x - halfSize,
      data.mainPosition.y - halfSize,
      data.mainSize,
      data.mainSize
    );
    ctx.strokeRect(
      data.mainPosition.x - halfSize,
      data.mainPosition.y - halfSize,
      data.mainSize,
      data.mainSize
    );

    // Draw center point
    ctx.fillStyle = '#1e293b';
    ctx.beginPath();
    ctx.arc(data.mainPosition.x, data.mainPosition.y, 3, 0, Math.PI * 2);
    ctx.fill();

    // Draw end mass (circle)
    ctx.fillStyle = '#f59e0b';
    ctx.strokeStyle = '#fbbf24';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(
      data.endMassPosition.x,
      data.endMassPosition.y,
      data.endMassRadius,
      0,
      Math.PI * 2
    );
    ctx.fill();
    ctx.stroke();

    // Draw velocity vector for end mass
    const velocityScale = 20;
    const velX = data.linearSpeed.x * velocityScale;
    const velY = data.linearSpeed.y * velocityScale;
    
    ctx.strokeStyle = '#10b981';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(data.endMassPosition.x, data.endMassPosition.y);
    ctx.lineTo(data.endMassPosition.x + velX, data.endMassPosition.y + velY);
    ctx.stroke();

    // Draw arrow head
    const angle = Math.atan2(velY, velX);
    const arrowSize = 8;
    ctx.fillStyle = '#10b981';
    ctx.beginPath();
    ctx.moveTo(
      data.endMassPosition.x + velX,
      data.endMassPosition.y + velY
    );
    ctx.lineTo(
      data.endMassPosition.x + velX - arrowSize * Math.cos(angle - Math.PI / 6),
      data.endMassPosition.y + velY - arrowSize * Math.sin(angle - Math.PI / 6)
    );
    ctx.lineTo(
      data.endMassPosition.x + velX - arrowSize * Math.cos(angle + Math.PI / 6),
      data.endMassPosition.y + velY - arrowSize * Math.sin(angle + Math.PI / 6)
    );
    ctx.closePath();
    ctx.fill();

    ctx.restore();

    // Draw labels
    ctx.fillStyle = '#94a3b8';
    ctx.font = '12px monospace';
    
    // Calculate and display rotation angle
    // const rotationAngle = Math.atan2(data.endMassPosition.y, data.endMassPosition.x);
    // const rotationDegrees = ((rotationAngle * 180 / Math.PI) + 360) % 360;
    // ctx.fillText(`Rotation: ${rotationDegrees.toFixed(1)}°`, 10, 40);

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
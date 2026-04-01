import React, { useRef, useEffect } from 'react';

const BG = '#1e293b';
const LINE_COLOR = '#3b82f6';
const MEAN_COLOR = '#facc15';
const GRID_COLOR = 'rgba(255,255,255,0.06)';

export default function SpeedSparkline({ samples, width = 400, height = 120 }) {
  const canvasRef = useRef(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const dpr = window.devicePixelRatio || 1;

    canvas.width = width * dpr;
    canvas.height = height * dpr;
    ctx.scale(dpr, dpr);

    // Background
    ctx.fillStyle = BG;
    ctx.fillRect(0, 0, width, height);

    if (samples.length < 2) {
      ctx.fillStyle = 'rgba(255,255,255,0.3)';
      ctx.font = '14px monospace';
      ctx.textAlign = 'center';
      ctx.fillText('Waiting for data...', width / 2, height / 2);
      return;
    }

    const pad = { top: 10, bottom: 10, left: 0, right: 0 };
    const plotW = width - pad.left - pad.right;
    const plotH = height - pad.top - pad.bottom;

    const maxVal = Math.max(...samples) * 1.15 || 1;
    const mean = samples.reduce((a, b) => a + b, 0) / samples.length;

    const toX = (i) => pad.left + (i / (samples.length - 1)) * plotW;
    const toY = (v) => pad.top + plotH - (v / maxVal) * plotH;

    // Horizontal grid lines
    ctx.strokeStyle = GRID_COLOR;
    ctx.lineWidth = 1;
    for (let i = 1; i <= 3; i++) {
      const y = pad.top + (plotH / 4) * i;
      ctx.beginPath();
      ctx.moveTo(pad.left, y);
      ctx.lineTo(width - pad.right, y);
      ctx.stroke();
    }

    // Mean reference line
    ctx.strokeStyle = MEAN_COLOR;
    ctx.lineWidth = 1;
    ctx.setLineDash([4, 4]);
    const meanY = toY(mean);
    ctx.beginPath();
    ctx.moveTo(pad.left, meanY);
    ctx.lineTo(width - pad.right, meanY);
    ctx.stroke();
    ctx.setLineDash([]);

    // Speed line
    ctx.strokeStyle = LINE_COLOR;
    ctx.lineWidth = 2;
    ctx.beginPath();
    samples.forEach((val, i) => {
      const x = toX(i);
      const y = toY(val);
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.stroke();

    // Fill under curve
    ctx.globalAlpha = 0.1;
    ctx.fillStyle = LINE_COLOR;
    ctx.beginPath();
    samples.forEach((val, i) => {
      const x = toX(i);
      const y = toY(val);
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });
    ctx.lineTo(toX(samples.length - 1), pad.top + plotH);
    ctx.lineTo(toX(0), pad.top + plotH);
    ctx.closePath();
    ctx.fill();
    ctx.globalAlpha = 1;

  }, [samples, width, height]);

  return (
    <canvas
      ref={canvasRef}
      style={{ width, height, borderRadius: 8, display: 'block' }}
    />
  );
}

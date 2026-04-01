import { useState, useRef, useCallback, useEffect } from 'react';

const COUNTDOWN_SECONDS = 3;
const GAME_DURATION = 5; // seconds
const MIN_SPEED_THRESHOLD = 0.3; // units/s — reject stationary markers

export default function useConstantVelocityGame(data) {
  const [gamePhase, setGamePhase] = useState('idle'); // idle | countdown | playing | result
  const [countdown, setCountdown] = useState(COUNTDOWN_SECONDS);
  const [timeRemaining, setTimeRemaining] = useState(GAME_DURATION);
  const [score, setScore] = useState(0);
  const [passed, setPassed] = useState(false);
  const [meanSpeed, setMeanSpeed] = useState(0);
  const [variationPct, setVariationPct] = useState(0);

  const speedSamples = useRef([]);
  const gameTimer = useRef(null);
  const countdownTimer = useRef(null);
  const gameStartTime = useRef(null);

  const currentSpeed = Math.sqrt(
    (data.linearSpeed?.x || 0) ** 2 + (data.linearSpeed?.y || 0) ** 2
  );

  // Collect samples during play
  useEffect(() => {
    if (gamePhase === 'playing') {
      speedSamples.current.push(currentSpeed);
    }
  }, [gamePhase, currentSpeed, data.timestamp]);

  const computeScore = useCallback(() => {
    const samples = speedSamples.current;
    if (samples.length < 10) {
      setScore(0);
      setPassed(false);
      setMeanSpeed(0);
      setVariationPct(100);
      return;
    }

    const mean = samples.reduce((a, b) => a + b, 0) / samples.length;
    setMeanSpeed(mean);

    if (mean < MIN_SPEED_THRESHOLD) {
      setScore(0);
      setPassed(false);
      setVariationPct(100);
      return;
    }

    const variance = samples.reduce((sum, s) => sum + (s - mean) ** 2, 0) / samples.length;
    const stddev = Math.sqrt(variance);
    const cv = stddev / mean;
    const pct = Math.round(cv * 100);
    setVariationPct(pct);

    const s = Math.max(0, Math.round(100 - cv * 200));
    setScore(s);
    setPassed(s >= 70);
  }, []);

  const cleanup = useCallback(() => {
    if (gameTimer.current) clearInterval(gameTimer.current);
    if (countdownTimer.current) clearInterval(countdownTimer.current);
    gameTimer.current = null;
    countdownTimer.current = null;
  }, []);

  const startGame = useCallback(() => {
    cleanup();
    speedSamples.current = [];
    setScore(0);
    setPassed(false);
    setMeanSpeed(0);
    setVariationPct(0);
    setCountdown(COUNTDOWN_SECONDS);
    setTimeRemaining(GAME_DURATION);
    setGamePhase('countdown');

    let c = COUNTDOWN_SECONDS;
    countdownTimer.current = setInterval(() => {
      c -= 1;
      if (c <= 0) {
        clearInterval(countdownTimer.current);
        countdownTimer.current = null;
        setGamePhase('playing');
        gameStartTime.current = Date.now();

        // Game timer — tick every 100ms for smooth countdown display
        gameTimer.current = setInterval(() => {
          const elapsed = (Date.now() - gameStartTime.current) / 1000;
          const remaining = Math.max(0, GAME_DURATION - elapsed);
          setTimeRemaining(remaining);

          if (remaining <= 0) {
            clearInterval(gameTimer.current);
            gameTimer.current = null;
            setGamePhase('result');
          }
        }, 100);
      }
      setCountdown(c);
    }, 1000);
  }, [cleanup]);

  // Compute score when entering result phase
  useEffect(() => {
    if (gamePhase === 'result') {
      computeScore();
    }
  }, [gamePhase, computeScore]);

  const resetGame = useCallback(() => {
    cleanup();
    speedSamples.current = [];
    setGamePhase('idle');
    setCountdown(COUNTDOWN_SECONDS);
    setTimeRemaining(GAME_DURATION);
    setScore(0);
    setPassed(false);
  }, [cleanup]);

  // Cleanup on unmount
  useEffect(() => cleanup, [cleanup]);

  return {
    gamePhase,
    countdown,
    timeRemaining,
    score,
    passed,
    meanSpeed,
    variationPct,
    currentSpeed,
    speedHistory: speedSamples.current,
    startGame,
    resetGame,
    gameDuration: GAME_DURATION,
  };
}

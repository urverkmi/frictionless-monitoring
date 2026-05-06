// EDIT THESE VALUES to tune the game to your physical setup.
// All world-frame values are in METRES (matching the C++ detector output).

export const GAME_CONFIG = {
  // ---- View transform (camera frame -> displayed world frame) ----
  // Applied to the puck's incoming position before rendering / hit detection.
  // Tune until pushing the puck physically right makes it move right on the
  // canvas, etc. Order: rotate -> mirror -> scale -> offset.
  viewRotationDeg: -90,    // try 0, 90, -90, 180. Camera mounted "sideways"? Use ±90.
  viewMirrorX:    false, // flip horizontally if the puck moves the wrong way left/right
  viewMirrorY:    false, // flip vertically if the puck moves the wrong way up/down
  viewScale:      1.0,   // overall scale on top of pixelsPerMetre (use to stretch)
  viewOffsetX:    -0.03,   // m, added after rotation/mirror/scale (recenter origin)
  viewOffsetY:    -0.4,   // m

  // ---- Table bounds (world frame, metres). Match your calibration rectangle. ----
  tableHalfWidth:  0.6,   // half of long axis (X). Default 0.8 = 1.6 m table.
  tableHalfHeight: 0.6,   // half of short axis (Y). Default 0.3 = 0.6 m table.

  // ---- Target ----
  targetRadius:             0.08,  // m. ~8 cm circle. Smaller = harder.
  targetMargin:             0.05,  // m. Min distance from table edge when spawning.
  minSpawnDistanceFromPuck: 0.20,  // m. Don't spawn under the puck.

  // ---- Round / scoring ----
  roundDurationSeconds: 30,    // max seconds per round. Hits target sooner = round ends.
  // Score = clamp( 100 * (1 - peakSpeed / maxAllowedSpeed), 0, 100 ) when hit, else 0.
  // Lower maxAllowedSpeed = harsher (any decent flick zeroes the score).
  maxAllowedSpeed:      0.50,  // m/s. Speed at which projected score reaches 0.

  // ---- Rendering ----
  pixelsPerMetre: 500,  // canvas scale. 500 px/m means 1.6 m -> 800 px wide.
  hitFlashMs:     300,  // duration of the on-hit flash animation.
};

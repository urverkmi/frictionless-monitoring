# API 

## 1) Detector Output (C++ -> Python)

Send one JSON message per frame to localhost UDP `9001`.

```json
{
  "timestamp": 1700000000.123,
  "frame_id": 1234,
  "camera_id": "cam0",
  "satellite_position": { "x": 100.0, "y": 100.0 },
  "end_mass_position": { "x": 140.2, "y": 132.8 },
  "orbital_angular_position": 1.57,
  "tracking_confidence": 0.92
}
```

Rules:
- `timestamp`: Unix seconds (float)
- `orbital_angular_position`: radians `[0, 2pi)`
- `tracking_confidence`: `[0.0, 1.0]`
- Tag mapping: `satellite = ID 0`, `end_mass = ID 1`
- Do not send legacy keys (`ts`, `frame`, `tag0`, `tag1`)

## 2) Internal Mapping (Python)

Python derives:
- `position = end_mass_position - satellite_position` (relative vector)
- `tether_length = |position|`

Kinematics uses relative position and orbital angle to compute:
- `velocity`, `acceleration`
- `angular_velocity`, `angular_acceleration`

## 3) GUI Payload (Python -> GUI)

```json
{
  "timestamp": 1700000000123,
  "satellitePosition": { "x": 100.0, "y": 100.0 },
  "endMassPosition": { "x": 140.2, "y": 132.8 },
  "linearSpeed": { "x": 1.2, "y": -0.4 },
  "angularSpeed": { "x": 0.5, "y": 0.1 },
  "tetherLength": 150.0
}
```

GUI requirements:
- Linear velocity: `x`, `y` 
- Angular velocity: `x`, `y` 
- Show satellite location (`x`, `y`)
- Show tether length (dynamic)
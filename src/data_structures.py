"""
Aurora Propulsion Plasma Brake Test Rig - Data Structures
Data models for position, velocity, and acceleration tracking
"""

from dataclasses import dataclass
from typing import Optional, List, Tuple
from datetime import datetime
import json


@dataclass
class Vector2D:
    """2D vector for position, velocity, or acceleration"""
    x: float
    y: float
    
    def magnitude(self) -> float:
        """Calculate vector magnitude"""
        return (self.x**2 + self.y**2)**0.5
    
    def to_dict(self) -> dict:
        return {'x': self.x, 'y': self.y}
    
    @classmethod
    def from_dict(cls, data: dict) -> 'Vector2D':
        return cls(x=data['x'], y=data['y'])


@dataclass
class FrameData:
    """Single frame measurement from the plasma brake test rig"""
    
    # Core measurements
    timestamp: float  # Unix timestamp in seconds
    frame_id: int  # Sequential frame number
    
    # Position (pixels or calibrated units)
    position: Vector2D
    
    # Velocity (units/second)
    velocity: Vector2D
    
    # Acceleration (units/second²)
    acceleration: Vector2D
    
    # Rotational data
    angular_position: float  # radians from reference
    angular_velocity: float  # rad/s
    angular_acceleration: float  # rad/s²
    
    # Quality metrics
    tracking_confidence: float  # 0.0 to 1.0
    detection_quality: Optional[str] = None  # 'good', 'medium', 'poor'
    
    def to_dict(self) -> dict:
        """Convert to dictionary for JSON serialization"""
        return {
            'timestamp': self.timestamp,
            'frame_id': self.frame_id,
            'position': self.position.to_dict(),
            'velocity': self.velocity.to_dict(),
            'acceleration': self.acceleration.to_dict(),
            'angular_position': self.angular_position,
            'angular_velocity': self.angular_velocity,
            'angular_acceleration': self.angular_acceleration,
            'tracking_confidence': self.tracking_confidence,
            'detection_quality': self.detection_quality
        }
    
    def to_json(self) -> str:
        """Serialize to JSON string"""
        return json.dumps(self.to_dict())
    
    @classmethod
    def from_dict(cls, data: dict) -> 'FrameData':
        """Deserialize from dictionary"""
        return cls(
            timestamp=data['timestamp'],
            frame_id=data['frame_id'],
            position=Vector2D.from_dict(data['position']),
            velocity=Vector2D.from_dict(data['velocity']),
            acceleration=Vector2D.from_dict(data['acceleration']),
            angular_position=data['angular_position'],
            angular_velocity=data['angular_velocity'],
            angular_acceleration=data['angular_acceleration'],
            tracking_confidence=data['tracking_confidence'],
            detection_quality=data.get('detection_quality')
        )
    
    @classmethod
    def from_json(cls, json_str: str) -> 'FrameData':
        """Deserialize from JSON string"""
        return cls.from_dict(json.loads(json_str))


@dataclass
class CalibrationData:
    """Camera and test rig calibration parameters"""
    
    # Physical calibration
    pixels_per_meter: float  # Conversion from pixels to meters
    center_point: Vector2D  # Rotation center in image coordinates
    
    # Camera parameters
    camera_resolution: Tuple[int, int]  # (width, height)
    fps: float
    
    # Test rig geometry
    arm_length: float  # meters
    end_mass: float  # kg
    
    # Timestamps
    calibration_timestamp: str
    
    def to_dict(self) -> dict:
        return {
            'pixels_per_meter': self.pixels_per_meter,
            'center_point': self.center_point.to_dict(),
            'camera_resolution': self.camera_resolution,
            'fps': self.fps,
            'arm_length': self.arm_length,
            'end_mass': self.end_mass,
            'calibration_timestamp': self.calibration_timestamp
        }
    
    def to_json(self) -> str:
        return json.dumps(self.to_dict())
    
    @classmethod
    def from_dict(cls, data: dict) -> 'CalibrationData':
        return cls(
            pixels_per_meter=data['pixels_per_meter'],
            center_point=Vector2D.from_dict(data['center_point']),
            camera_resolution=tuple(data['camera_resolution']),
            fps=data['fps'],
            arm_length=data['arm_length'],
            end_mass=data['end_mass'],
            calibration_timestamp=data['calibration_timestamp']
        )


@dataclass
class TestSession:
    """Complete test session with metadata"""
    
    session_id: str
    start_time: str
    end_time: Optional[str] = None
    calibration: Optional[CalibrationData] = None
    frames: List[FrameData] = None
    notes: str = ""
    
    def __post_init__(self):
        if self.frames is None:
            self.frames = []
    
    def add_frame(self, frame: FrameData):
        """Add a frame to the session"""
        self.frames.append(frame)
    
    def get_summary(self) -> dict:
        """Get session summary statistics"""
        if not self.frames:
            return {'frame_count': 0}
        
        velocities = [f.velocity.magnitude() for f in self.frames]
        angular_vels = [f.angular_velocity for f in self.frames]
        
        return {
            'session_id': self.session_id,
            'frame_count': len(self.frames),
            'duration': self.frames[-1].timestamp - self.frames[0].timestamp if len(self.frames) > 1 else 0,
            'avg_velocity': sum(velocities) / len(velocities),
            'max_velocity': max(velocities),
            'avg_angular_velocity': sum(angular_vels) / len(angular_vels),
            'max_angular_velocity': max(angular_vels)
        }
    
    def to_dict(self) -> dict:
        return {
            'session_id': self.session_id,
            'start_time': self.start_time,
            'end_time': self.end_time,
            'calibration': self.calibration.to_dict() if self.calibration else None,
            'frames': [f.to_dict() for f in self.frames],
            'notes': self.notes
        }
    
    def save_to_file(self, filename: str):
        """Save session to JSON file"""
        with open(filename, 'w') as f:
            json.dump(self.to_dict(), f, indent=2)
    
    @classmethod
    def load_from_file(cls, filename: str) -> 'TestSession':
        """Load session from JSON file"""
        with open(filename, 'r') as f:
            data = json.load(f)
        
        session = cls(
            session_id=data['session_id'],
            start_time=data['start_time'],
            end_time=data.get('end_time'),
            calibration=CalibrationData.from_dict(data['calibration']) if data.get('calibration') else None,
            notes=data.get('notes', '')
        )
        
        if data.get('frames'):
            session.frames = [FrameData.from_dict(f) for f in data['frames']]
        
        return session


# Example usage
if __name__ == "__main__":
    # Create sample data
    position = Vector2D(x=320.5, y=240.3)
    velocity = Vector2D(x=15.2, y=-8.7)
    acceleration = Vector2D(x=0.5, y=1.2)
    
    frame = FrameData(
        timestamp=1234567890.123,
        frame_id=42,
        position=position,
        velocity=velocity,
        acceleration=acceleration,
        angular_position=1.57,  # ~90 degrees
        angular_velocity=3.14,  # rad/s
        angular_acceleration=0.1,
        tracking_confidence=0.95,
        detection_quality='good'
    )
    
    # Convert to JSON for transmission
    json_string = frame.to_json()
    print("Frame as JSON:")
    print(json_string)
    
    # Parse back from JSON
    reconstructed = FrameData.from_json(json_string)
    print(f"\nVelocity magnitude: {reconstructed.velocity.magnitude():.2f} units/s")
    
    # Create a test session
    session = TestSession(
        session_id="test_001",
        start_time="2025-11-19T10:30:00",
        notes="Initial friction test"
    )
    session.add_frame(frame)
    
    print(f"\nSession summary:")
    print(session.get_summary())
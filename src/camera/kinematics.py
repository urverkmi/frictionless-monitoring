import math
from memory import MemoryManager
from typing import Optional, Tuple, List
from data_structures import FrameData, Vector2D


class KinematicsCalculator:
    """
    Calculate velocity and acceleration from position history.
    
    Responsibilities:
    - Read position data from MemoryManager
    - Calculate linear velocity (dx/dt, dy/dt)
    - Calculate linear acceleration (dv/dt)
    - Calculate angular velocity and acceleration
    - Create complete FrameData objects
    - Store FrameData for streaming
    
    """
    
    def __init__(self, memory_manager: MemoryManager):
        """
        Initialize calculator with reference to memory manager.
        
        Args:
            memory_manager: MemoryManager instance to read from
        """
        self.memory_manager = memory_manager
        self.frame_data_buffer: List[FrameData] = []
        pass

    @staticmethod
    def _relative_position(position_data: dict) -> Vector2D:
        """Relative end-mass position with respect to satellite."""
        sat = position_data["satellite_position"]
        end = position_data["end_mass_position"]
        return Vector2D(x=end.x - sat.x, y=end.y - sat.y)

    @staticmethod
    def _orbital_angle(position_data: dict) -> float:
        """Orbital angle around satellite in [0, 2π)."""
        if "orbital_angular_position" in position_data:
            return float(position_data["orbital_angular_position"])
        rel = KinematicsCalculator._relative_position(position_data)
        return (math.atan2(rel.y, rel.x) + 2 * math.pi) % (2 * math.pi)
    
    def calculate_velocity(self, current_pos: dict, 
                          previous_pos: dict) -> Vector2D:
        """
        Calculate velocity from two position measurements.
        
        Args:
            current_pos: Current position data dict
            previous_pos: Previous position data dict
            
        Returns:
            Velocity as Vector2D (units/second)
        
        """
        # Positions come from MemoryManager as Vector2D instances
        pos_cur = current_pos["position"]
        pos_prev = previous_pos["position"]
        # Use frame timestamps for proper physical units
        t_cur = current_pos["timestamp"]
        t_prev = previous_pos["timestamp"]
        dt = t_cur - t_prev
        # Guard against duplicate / out‑of‑order timestamps
        if dt <= 0:
            return Vector2D(0.0, 0.0)
        dx = pos_cur.x - pos_prev.x
        dy = pos_cur.y - pos_prev.y
        return Vector2D(dx / dt, dy / dt)
    
    def calculate_acceleration(self, current_vel: Vector2D, 
                              previous_vel: Vector2D,
                              dt: float) -> Vector2D:
        """
        Calculate acceleration from two velocity measurements.
        
        Args:
            current_vel: Current velocity
            previous_vel: Previous velocity
            dt: Time difference
            
        Returns:
            Acceleration as Vector2D (units/second²)
            
        """
        # Do not divide by zero or negative time deltas
        if dt <= 0:
            return Vector2D(0.0, 0.0)
        dvx = current_vel.x - previous_vel.x
        dvy = current_vel.y - previous_vel.y
        return Vector2D(dvx / dt, dvy / dt)
    
    def calculate_angular_velocity(self, current_angle: float,
                                   previous_angle: float,
                                   dt: float) -> float:
        """
        Calculate angular velocity.
        
        Args:
            current_angle: Current angular position (radians)
            previous_angle: Previous angular position (radians)
            dt: Time difference
            
        Returns:
            Angular velocity (rad/s)

        """
        # No time elapsed → no angular motion defined
        if dt <= 0:
            return 0.0
        dtheta = current_angle - previous_angle
        # Wraparound: shortest path in [-π, π]
        dtheta = (dtheta + math.pi) % (2 * math.pi) - math.pi
        return dtheta / dt
    
    def calculate_angular_acceleration(self, current_omega: float,
                                       previous_omega: float,
                                       dt: float) -> float:
        """
        Calculate angular acceleration.
        
        Args:
            current_omega: Current angular velocity
            previous_omega: Previous angular velocity
            dt: Time difference
            
        Returns:
            Angular acceleration (rad/s²)
            
        """
        if dt <= 0:
            return 0.0
        return (current_omega - previous_omega) / dt
    
    def process_frame(self) -> Optional[FrameData]:
        """
        Process latest data and create complete FrameData object.
        
        Returns:
            FrameData object with all calculated values, or None if 
            insufficient data in buffer

        """
        # Most recent N position measurements from MemoryManager
        positions = self.memory_manager.get_recent_positions(n=3)
        if len(positions) < 2:
            return None

        # Buffer is ordered newest first.
        current_pos = positions[0]
        previous_pos = positions[1]
        required_keys = ("satellite_position", "end_mass_position")
        if not all(k in current_pos for k in required_keys):
            return None
        if not all(k in previous_pos for k in required_keys):
            return None
        dt_vel = current_pos["timestamp"] - previous_pos["timestamp"]
        if dt_vel <= 0:
            return None

        # Build relative-position snapshots for derivative calculations.
        current_rel = {
            "timestamp": current_pos["timestamp"],
            "position": self._relative_position(current_pos),
        }
        previous_rel = {
            "timestamp": previous_pos["timestamp"],
            "position": self._relative_position(previous_pos),
        }

        velocity = self.calculate_velocity(current_rel, previous_rel)
        angular_velocity = self.calculate_angular_velocity(
            self._orbital_angle(current_pos),
            self._orbital_angle(previous_pos),
            dt_vel,
        )

        acceleration = Vector2D(0.0, 0.0)
        angular_acceleration = 0.0
        if len(positions) >= 3:
            older_pos = positions[2]
            if not all(k in older_pos for k in required_keys):
                return None
            dt_prev = previous_pos["timestamp"] - older_pos["timestamp"]
            if dt_prev > 0:
                older_rel = {
                    "timestamp": older_pos["timestamp"],
                    "position": self._relative_position(older_pos),
                }
                previous_vel = self.calculate_velocity(previous_rel, older_rel)
                acceleration = self.calculate_acceleration(
                    velocity, previous_vel, (dt_vel + dt_prev) / 2.0
                )
                prev_angular = self.calculate_angular_velocity(
                    self._orbital_angle(previous_pos),
                    self._orbital_angle(older_pos),
                    dt_prev,
                )
                angular_acceleration = self.calculate_angular_acceleration(
                    angular_velocity, prev_angular, (dt_vel + dt_prev) / 2.0
                )

        relative_position = self._relative_position(current_pos)
        satellite_position = current_pos.get("satellite_position", Vector2D(0.0, 0.0))
        end_mass_position = current_pos.get("end_mass_position", relative_position)
        frame_data = FrameData(
            timestamp=current_pos["timestamp"],
            frame_id=current_pos["frame_id"],
            satellite_position=satellite_position,
            end_mass_position=end_mass_position,
            position=relative_position,
            tether_length=relative_position.magnitude(),
            velocity=velocity,
            acceleration=acceleration,
            angular_position=self._orbital_angle(current_pos),
            angular_velocity=angular_velocity,
            angular_acceleration=angular_acceleration,
            tracking_confidence=current_pos.get("tracking_confidence", 1.0),
            detection_quality=current_pos.get("detection_quality"),
        )
        self.frame_data_buffer.append(frame_data)
        return frame_data
    
    def get_latest_frame_data(self) -> Optional[FrameData]:
        """
        Get the most recently calculated FrameData.
        
        Returns:
            Latest FrameData object or None
            
        TODO: Implement retrieval from buffer
        """
        if not self.frame_data_buffer:
            return None
        return self.frame_data_buffer[-1]

    def get_frame_data_buffer(self) -> List[FrameData]:
        """
        Get all stored FrameData objects.
        
        Returns:
            List of FrameData objects
            
        TODO: Return current buffer contents
        """
        return list(self.frame_data_buffer)

    def clear_buffer(self):
        """
        Clear FrameData buffer.
        
        TODO: Implement buffer clearing
        """
        self.frame_data_buffer.clear()

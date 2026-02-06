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
    
    def calculate_velocity(self, current_pos: dict, 
                          previous_pos: dict) -> Vector2D:
        """
        Calculate velocity from two position measurements.
        
        Args:
            current_pos: Current position data dict
            previous_pos: Previous position data dict
            
        Returns:
            Velocity as Vector2D (units/second)
            
        TODO: Implement velocity calculation
        - Extract positions and timestamps
        - Calculate dx = x1 - x0, dy = y1 - y0
        - Calculate dt = t1 - t0
        - Return Vector2D(dx/dt, dy/dt)
        - Handle dt = 0 case
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
            
        TODO: Implement acceleration calculation
        - Calculate dvx = vx1 - vx0, dvy = vy1 - vy0
        - Return Vector2D(dvx/dt, dvy/dt)
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
            
        TODO: Implement angular velocity
        - Calculate dθ = θ1 - θ0
        - Handle wraparound at 2π (shortest path)
        - Return dθ/dt
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
            
        TODO: Implement angular acceleration
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
            
        TODO: Implement complete processing pipeline
        1. Get last 3-4 positions from MemoryManager
        2. Check if enough data available (need at least 2 for velocity)
        3. Calculate velocity from last 2 positions
        4. Calculate acceleration (need at least 3 positions)
        5. Calculate angular velocity and acceleration
        6. Create FrameData object
        7. Store in self.frame_data_buffer
        8. Return FrameData
        
        IMPORTANT: Decide where to store FrameData objects:
        - Option A: Keep in self.frame_data_buffer (list) for later retrieval
        - Option B: Immediately pass to DataStreamer for transmission
        - Option C: Both - store locally AND stream
        """
        # Most recent N position measurements from MemoryManager
        positions = self.memory_manager.get_recent_positions(n=3)
        if len(positions) < 2:
            return None

        # Buffer is ordered newest first:
        # positions[0] = current, positions[1] = previous, positions[2] = older
        current_pos = positions[0]
        previous_pos = positions[1]
        dt_vel = current_pos["timestamp"] - previous_pos["timestamp"]
        if dt_vel <= 0:
            return None

        velocity = self.calculate_velocity(current_pos, previous_pos)
        angular_velocity = self.calculate_angular_velocity(
            current_pos["angular_position"],
            previous_pos["angular_position"],
            dt_vel,
        )

        acceleration = Vector2D(0.0, 0.0)
        angular_acceleration = 0.0
        if len(positions) >= 3:
            older_pos = positions[2]
            dt_prev = previous_pos["timestamp"] - older_pos["timestamp"]
            if dt_prev > 0:
                previous_vel = self.calculate_velocity(previous_pos, older_pos)
                acceleration = self.calculate_acceleration(
                    velocity, previous_vel, (dt_vel + dt_prev) / 2.0
                )
                prev_angular = self.calculate_angular_velocity(
                    previous_pos["angular_position"],
                    older_pos["angular_position"],
                    dt_prev,
                )
                angular_acceleration = self.calculate_angular_acceleration(
                    angular_velocity, prev_angular, (dt_vel + dt_prev) / 2.0
                )

        frame_data = FrameData(
            timestamp=current_pos["timestamp"],
            frame_id=current_pos["frame_id"],
            position=current_pos["position"],
            velocity=velocity,
            acceleration=acceleration,
            angular_position=current_pos["angular_position"],
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

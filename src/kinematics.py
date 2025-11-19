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
    
    TEAMMATE IMPLEMENTATION NEEDED
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
        pass
    
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
        pass
    
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
        pass
    
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
        pass
    
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
        pass
    
    def get_latest_frame_data(self) -> Optional[FrameData]:
        """
        Get the most recently calculated FrameData.
        
        Returns:
            Latest FrameData object or None
            
        TODO: Implement retrieval from buffer
        """
        pass
    
    def get_frame_data_buffer(self) -> List[FrameData]:
        """
        Get all stored FrameData objects.
        
        Returns:
            List of FrameData objects
            
        TODO: Return current buffer contents
        """
        pass
    
    def clear_buffer(self):
        """
        Clear FrameData buffer.
        
        TODO: Implement buffer clearing
        """
        pass

from collections import deque
from typing import Optional, Tuple, List



class MemoryManager:
    """
    Manage position data in memory for derivative calculations.
    
    Responsibilities:
    - Store recent position measurements in ring buffer
    - Provide access to historical data for velocity/acceleration calculation
    - Manage memory efficiently (auto-cleanup old data)
    - Thread-safe operations
    
    """
    
    def __init__(self, buffer_size: int = 100):
        """
        Initialize memory manager with ring buffer.
        
        Args:
            buffer_size: Maximum number of frames to keep in memory
                        (default 100 = ~3 seconds at 30fps)
        """
        self.buffer_size = buffer_size
        self.position_buffer: deque = deque(maxlen=buffer_size)

    def store_position_data(self, position_data: dict):
        """
        Store position data from upstream detector input.
        
        Args:
            position_data: Dictionary from the active input source
                          (e.g., C++ bridge, fake stream, or Python detector).
                          Expected keys:
                          - timestamp
                          - frame_id
                          - satellite_position
                          - end_mass_position
                          - orbital_angular_position
                          - tracking_confidence
        
        Notes:
        - Ring buffer automatically removes oldest entries at capacity.
        """
        if not position_data:
            return
        self.position_buffer.append(position_data)

    def get_recent_positions(self, n: int = 2) -> List[dict]:
        """
        Get the N most recent position measurements.
        
        Args:
            n: Number of recent positions to retrieve
            
        Returns:
            List of position data dictionaries (newest first)

        """
        if n <= 0 or not self.position_buffer:
            return []
        buf = list(self.position_buffer)
        k = min(n, len(buf))
        return list(reversed(buf[-k:]))
    
    def get_position_at_time(self, target_time: float) -> Optional[dict]:
        """
        Get position data closest to a specific timestamp.
        
        Args:
            target_time: Target timestamp
            
        Returns:
            Position data closest to target time, or None if buffer empty
        Note:
            Currently unused.
            Kept for future synchronization/replay use cases.
        """
        return None
    
    def clear_buffer(self):
        """
        Clear all data from memory buffer.
        """
        self.position_buffer.clear()

    def get_buffer_info(self) -> dict:
        """
        Get information about current buffer state.
        
        Returns:
            Dictionary with buffer statistics:
            {
                'size': int,  # Current number of items
                'capacity': int,  # Maximum capacity
                'oldest_timestamp': float,
                'newest_timestamp': float
            }
        """
        if not self.position_buffer:
            return {
                "size": 0,
                "capacity": self.buffer_size,
                "oldest_timestamp": None,
                "newest_timestamp": None,
            }
        buf = list(self.position_buffer)
        return {
            "size": len(buf),
            "capacity": self.buffer_size,
            "oldest_timestamp": buf[0].get("timestamp"),
            "newest_timestamp": buf[-1].get("timestamp"),
        }

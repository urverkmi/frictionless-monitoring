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
        Store position data from PositionDetector.
        
        Args:
            position_data: Dictionary from PositionDetector.read_position()
                          Contains: timestamp, frame_id, position, 
                                   angular_position, tracking_confidence
        
        TODO: Implement storage
        - Add to ring buffer (deque automatically removes oldest)
        - Consider thread safety if needed (threading.Lock)
        - Validate data before storing
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
            
        TODO: Implement retrieval
        - Return last N items from buffer
        - Handle case where buffer has fewer than N items
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
            
        TODO: Optional - implement time-based lookup
        """
        pass
    
    def clear_buffer(self):
        """
        Clear all data from memory buffer.
        
        TODO: Implement buffer clearing
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
            
        TODO: Implement buffer info
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

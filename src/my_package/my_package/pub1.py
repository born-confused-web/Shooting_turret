import rclpy
from rclpy.node import Node
from std_msgs.msg import Int64

class NumberPublisher(Node):
    def __init__(self):
        super().__init__("sender_node")
        self.publisher_ = self.create_publisher(Int64, 'line123', 10)
        self.timer = self.create_timer(1.0, self.example_function)
        self.counter = 0
    
    def example_function(self):
        mess = Int64()
        mess.data = self.counter
        self.publisher_.publish(mess)
        self.get_logger().info(f"your message is {self.counter}")
        self.counter += 1

def main(args=None):
    rclpy.init(args=args)
    node = NumberPublisher()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()

if __name__ == "__main__":
    main()
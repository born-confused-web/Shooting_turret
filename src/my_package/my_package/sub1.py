import rclpy
from rclpy.node import Node
from std_msgs.msg import Int64, String

class NumberSuscriber(Node):
    def __init__(self):
        super().__init__("number_sus")
        self.suscriber_ = self.create_subscription(Int64, "line123", self.example2_function, 10)
        self.subscriber2_ = self.create_subscription(String, "key_line", self.key_display_function, 20)

    def example2_function(self, mess):
        self.get_logger().info(f"I heard {mess.data}")

    def key_display_function(self, mess):
        self.get_logger().info(f"move:{mess.data}")
        
def main(args=None):
    rclpy.init(args=args)
    node = NumberSuscriber()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()

if __name__ == '__main__':
    main()
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import JointState

class JointStateSubscriber(Node):
    def __init__(self):
        super().__init__("take_angles")
        self.subscriber_ = self.create_subscription(JointState, "joint_states", self.display_fn, 50)
    
    def display_fn(self, mess):
        self.get_logger().info(f"jointstate: {mess.data}")
    
def main(args=None):
    rclpy.init(args=args)
    node = JointStateSubscriber()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()

if __name__=="__main__":
    main()
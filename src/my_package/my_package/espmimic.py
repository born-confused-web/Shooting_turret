import rclpy
from rclpy.node import Node
from std_msgs.msg import Float32

class Esp32(Node):
    def __init__(self):
        super().__init__("esp32")
        self.subscriber_ = self.create_subscription(Float32, "joint_angle", self.rotatefn, 10)
        self.publisher_ = self.create_publisher(Float32, "feedback", 10)
        self.timer = self.create_timer(1.0, self.feedbackfn)
    
    def rotatefn(self, mess):
        print(f"esp rotated motor to{mess.data}")
        self.message = mess.data + 1000.0
    
    def feedbackfn(self):
        jointfeed = Float32()
        jointfeed.data = float(self.message)
        self.publisher_.publish(jointfeed)
    
def main(args=None):
    rclpy.init(args=args)
    node = Esp32()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()

if __name__=="__main__":
    main()
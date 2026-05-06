import rclpy
from rclpy.node import Node
from std_msgs.msg import Float32
from pynput import keyboard

class EspJointPublisher(Node):
    def __init__(self):
        super().__init__("com_node")
        self.publisher_ = self.create_publisher(Float32, "joint_angle", 10)
        self.subscriber_ = self.create_subscription(Float32, "feedback", self.subscriber_fn, 10)

        self.timer = self.create_timer(0.1, self.sendingfn)
        self.listener = keyboard.Listener(on_press=self.on_press, on_release=self.on_release)
        self.listener.start()
        
        self.angle = 0.0
        self.get_logger().info("Node started. Use 'a' and 'd' to move. ESC to stop.")

    def on_press(self, key):
        try:
            if hasattr(key, 'char'):
                if key.char == 'a':
                    self.angle -= 1.0
                elif key.char == 'd':
                    self.angle += 1.0
                
                self.angle = max(0.0, min(180.0, self.angle))
        except Exception as e:
            self.get_logger().error(f"Error in key press: {e}")
    
    def on_release(self, key):
        if key == keyboard.Key.esc:
            return False

    def sendingfn(self):
        msg = Float32()
        msg.data = self.angle
        self.publisher_.publish(msg)

    def subscriber_fn(self, mess):
        self.get_logger().info(f"Feedback:{mess.data}")
        self.get_logger().info("----------------------------------------------")
    
def main(args=None):
    rclpy.init(args=args)
    node = EspJointPublisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()

if __name__ == "__main__":
    main()
    
    


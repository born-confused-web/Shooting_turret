import rclpy
from rclpy.node import Node
from std_msgs.msg import Float64MultiArray
from pynput import keyboard

class KeyboardPub(Node):
    def __init__(self):
        super().__init__("key_node")
        self.publisher_ = self.create_publisher(Float64MultiArray, "commands", 10)
        self.timer = self.create_timer(1.0, self.key_inputs)
        self.listner = keyboard.Listener(on_press=self.is_pressed, on_release=self.is_released)
        self.listner.start()
        self.vertical_angle = 0
        self.horizontal_angle = 0
    
    def is_pressed(self, key):
        try:
            if key.char=="w":
                if self.vertical_angle<1.5708:
                    self.vertical_angle+=0.1745
            
            elif key.char=="s":
                if self.vertical_angle>-1.5708:
                    self.vertical_angle-=0.1745
            
            elif key.char=="a":
                if self.horizontal_angle<1.5708:
                    self.horizontal_angle+=0.1745
            
            elif key.char=="d":
                if self.horizontal_angle>-1.5708:
                    self.horizontal_angle-=0.1745
        
        except AttributeError:
            pass
    
    def is_released(self, key):
        if key == keyboard.Key.esc:
            return False
        
    def key_inputs(self):
        joint_angles = Float64MultiArray()
        joint_angles.data = [self.horizontal_angle, self.vertical_angle]
        self.publisher_.publish(joint_angles)

def main(args=None):
    rclpy.init(args=args)
    node = KeyboardPub()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()

if __name__ == "__main__":
    main()






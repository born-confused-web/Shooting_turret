import rclpy
from rclpy.node import Node
from std_msgs.msg import String
from pynput import keyboard

class KeyboardPub(Node):
    def __init__(self):
        super().__init__("key_node")
        self.publisher_ = self.create_publisher(String, "key_line", 10)
        self.current_key = "stop"  # Initialize the variable
        self.timer = self.create_timer(1.0, self.key_inputs)
        self.listner = keyboard.Listener(on_press=self.is_pressed, on_release=self.is_released)
        self.listner.start()
    
    def is_pressed(self, key):
        try:
            if key.char=="w":
                self.current_key = "forward"
            
            elif key.char=="s":
                self.current_key = "backward"
            
            elif key.char=="a":
                self.current_key = "left"
            
            elif key.char=="d":
                self.current_key = "right"
        
        except AttributeError:
            pass
    
    def is_released(self, key):
        self.current_key = "stop"
        if key == keyboard.Key.esc:
            return False
        
    def key_inputs(self):
        key_message = String()
        key_message.data = str(self.current_key)
        self.publisher_.publish(key_message)

def main(args=None):
    rclpy.init(args=args)
    node = KeyboardPub()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()

if __name__ == "__main__":
    main()






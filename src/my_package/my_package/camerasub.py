import rclpy
from rclpy.node import Node
from sensor_msgs.msg import CompressedImage
from std_msgs.msg import Int64
import cv2 as cv
import numpy as np

class CameraFeed(Node):
    def __init__(self):
        super().__init__("camerafeed")
        self.subscriber_ = self.create_subscription(CompressedImage, "camerashit", self.camera_callback, 10)
        self.subscriber2_ = self.create_subscription(Int64, "feedback_mess", self.feedback_callback, 10)
    
    def feedback_callback(self, mess):
        self.get_logger().info(f"msg from esp: {mess.data}")

    def camera_callback(self, mess):
        image_arr = np.frombuffer(mess.data, np.uint8)
        frame = cv.imdecode(image_arr, cv.IMREAD_COLOR)

        if frame is not None:
            cv.imshow("feed", frame)
            cv.waitKey(1)
        else:
            self.get_logger().info("no frame recieved")

def main(args=None):
    rclpy.init(args=args)
    node = CameraFeed()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()

if __name__=="__main__":
    main()
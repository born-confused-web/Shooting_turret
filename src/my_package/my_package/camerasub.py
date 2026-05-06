import rclpy
from rclpy.node import Node
from sensor_msgs.msg import CompressedImage
import cv2 as cv
import numpy as np

class CameraFeed(Node):
    def __init__(self):
        super().__init__("camerafeed")
        self.subscriber_ = self.create_subscription(CompressedImage, "camerashit", self.camera_callback, 10)

    def camera_callback(self, mess):
        image_arr = np.frombuffer(mess.data.data, np.uint8)
        frame = cv.imdecode(image_arr, cv.IMREAD_COLOR)

        if frame is not None:
            cv.imshow("feed", frame)
        else:
            self.get_logger().info("no frame recieved")

def main(args=None):
    rclpy.init(args=args)
    node = CameraFeed()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        print("camerasub closed")
    node.destroy_node()
    rclpy.shutdown()

if __name__=="__main__":
    main()
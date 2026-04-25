from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription, ExecuteProcess
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os
from launch_ros.substitutions import FindPackageShare
import xacro

def generate_launch_description():

    pkg_my_robot = get_package_share_directory('my_robot_description')

    urdf_file = os.path.join(pkg_my_robot, 'urdf', 'turret.urdf.xacro')

    robot_description = xacro.process_file(urdf_file).toxml()

    controller_yaml = os.path.join(pkg_my_robot, 'config', 'controllers.yaml')

    gz_sim = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(get_package_share_directory('ros_gz_sim'), 'launch', 'gz_sim.launch.py')
        ),
        launch_arguments=[('gz_args', '-r empty.sdf')]
    )

    control_node = Node(
        package='controller_manager',
        executable='ros2_control_node',
        parameters=[
            {'robot_description': robot_description},
            controller_yaml
        ],
        output='screen'
    )

    bridge = Node(
        package='ros_gz_bridge',
        executable='parameter_bridge',
        arguments=[
            '/clock@rosgraph_msgs/msg/Clock[gz.msgs.Clock]',
        ],
        output='screen'
    )

    robot_state_publisher = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        name='robot_state_publisher',
        output='screen',
        parameters=[{
            'robot_description': robot_description
        }]
    )

    spawn_robot = Node(
        package='ros_gz_sim',
        executable='create',
        arguments=[
            '-name', 'turret',
            '-topic', '/robot_description',
            '-x', '0.0',
            '-y', '0.0',
            '-z', '0.0'
        ],
        output='screen'
    )

    joint_node = Node(
        package='my_package',
        executable='key_ios',
        output='screen'
    )

    joint_display_node = Node(
        package='my_package',
        executable='jointangledisplay',
        output='screen'

    )

    return LaunchDescription([
        robot_state_publisher,
        gz_sim,
        joint_node,
        control_node,
        spawn_robot,
        bridge,
        joint_display_node
    ])
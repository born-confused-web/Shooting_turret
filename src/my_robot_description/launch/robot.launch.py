from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import Command
from launch_ros.descriptions import ParameterValue
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory, get_package_share_path
from launch.actions import SetEnvironmentVariable
from launch.actions import RegisterEventHandler
from launch.event_handlers import OnProcessExit
import os

def generate_launch_description():

    urdf_file = get_package_share_path("my_robot_description")/'urdf'/'turret.urdf.xacro'

    controller_yaml = get_package_share_path("my_robot_description")/'config'/'controller.yaml'

    set_gazebo_model_path = SetEnvironmentVariable(
        name='GZ_SIM_RESOURCE_PATH',
        value=os.path.dirname(get_package_share_directory('my_robot_description'))
    )

    gz_sim = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(get_package_share_directory('ros_gz_sim'), 'launch', 'gz_sim.launch.py')
        ),
        launch_arguments=[('gz_args', '-r empty.sdf')]
    )


    control_node = Node(
        package='controller_manager',
        executable='ros2_control_node',
        name='controller_manager',
        parameters=[
            controller_yaml
        ],
        output='screen'
    )

    load_joint_state = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["joint_state_broadcaster"],
        output="screen",
    )

    load_position_controller = Node(
        package="controller_manager",
        executable="spawner",
        arguments=["position_controller"],
        output="screen",
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
            'robot_description': ParameterValue(
                Command(['xacro ', str(urdf_file)]), value_type=str
            )
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

    joint_display_node = Node(
        package='my_package',
        executable='jointangledisplay',
        output='screen'

    )

    return LaunchDescription([
        set_gazebo_model_path,
        gz_sim,
        bridge,
        spawn_robot,
        robot_state_publisher,
        control_node,
        RegisterEventHandler(
            OnProcessExit(
                target_action=control_node,
                on_exit=[spawn_robot],
            )
        ),
        RegisterEventHandler(
            OnProcessExit(
                target_action=spawn_robot,
                on_exit=[load_joint_state],
            )
        ),

        RegisterEventHandler(
            OnProcessExit(
                target_action=load_joint_state,
                on_exit=[load_position_controller],
            )
        ),
        joint_display_node
    ])
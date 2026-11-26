"""Starts the drone side of Mini-Acrobat:

  safety_node        arm/disarm state machine + GCS link watchdog
  link_monitor       drone network status on /link/status (Wi-Fi signal, IPs, Tailscale)
  rosbridge_server   WebSocket for the MAgcs app (control + data), port 9090
  foxglove_bridge    read-only WebSocket for Foxglove (data only), port 8765

  ros2 launch mini_acrobat_bringup bringup.launch.py
  ros2 launch mini_acrobat_bringup bringup.launch.py foxglove:=false rosbridge_port:=9091
"""
import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    share = get_package_share_directory('mini_acrobat_bringup')
    safety_launch = os.path.join(
        get_package_share_directory('mini_acrobat_safety'), 'launch', 'safety.launch.py')

    return LaunchDescription([
        DeclareLaunchArgument('rosbridge_port', default_value='9090'),
        DeclareLaunchArgument('foxglove', default_value='true'),
        DeclareLaunchArgument('foxglove_port', default_value='8765'),
        DeclareLaunchArgument('wifi_interface', default_value='wlan0'),

        IncludeLaunchDescription(PythonLaunchDescriptionSource(safety_launch)),

        Node(
            package='mini_acrobat_link',
            executable='link_monitor',
            name='link_monitor',
            output='screen',
            parameters=[
                os.path.join(get_package_share_directory('mini_acrobat_link'), 'config', 'link.yaml'),
                {'interface': LaunchConfiguration('wifi_interface')},
            ],
        ),

        Node(
            package='rosbridge_server',
            executable='rosbridge_websocket',
            name='rosbridge_websocket',
            output='screen',
            parameters=[
                os.path.join(share, 'config', 'rosbridge.yaml'),
                {'port': ParameterValue(LaunchConfiguration('rosbridge_port'), value_type=int)},
            ],
        ),

        Node(
            package='foxglove_bridge',
            executable='foxglove_bridge',
            name='foxglove_bridge',
            output='screen',
            condition=IfCondition(LaunchConfiguration('foxglove')),
            parameters=[
                os.path.join(share, 'config', 'foxglove.yaml'),
                {'port': ParameterValue(LaunchConfiguration('foxglove_port'), value_type=int)},
            ],
        ),
    ])

import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    params = os.path.join(
        get_package_share_directory('mini_acrobat_safety'), 'config', 'safety.yaml')
    return LaunchDescription([
        Node(
            package='mini_acrobat_safety',
            executable='safety_node',
            name='safety_node',
            output='screen',
            emulate_tty=True,
            parameters=[params],
        ),
    ])

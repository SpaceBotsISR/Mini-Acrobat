from setuptools import setup

package_name = 'mini_acrobat_link'

setup(
    name=package_name,
    version='0.1.0',
    packages=[package_name],
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        ('share/' + package_name + '/config', ['config/link.yaml']),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='Francisco Branco',
    maintainer_email='kiko.castelobranco@gmail.com',
    description='Drone network status (/link/status) for MAgcs',
    license='MIT',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'link_monitor = mini_acrobat_link.link_monitor:main',
        ],
    },
)

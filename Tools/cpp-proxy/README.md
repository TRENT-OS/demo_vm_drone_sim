# Cpp Proxy 

This application is a proxy to fetch available sensor data from the gazebo simulator and forward it to the TrentOS SimCoupler component.
It is not running as part of the TrentOS system and needs to be started by the user seperately on the **host** system outside of the TrentOS Docker environment. It therefore is not part of the TrentOS build system.


## Installation
### Requirements
The cpp_proxy application requires the gazebo [transport library](https://gazebosim.org/api/transport/11.0/). 

#### Ubuntu Install
Repository setup:
```sh
sudo sh -c 'echo "deb http://packages.osrfoundation.org/gazebo/ubuntu-stable `lsb_release -cs` main" > /etc/apt/sources.list.d/gazebo-stable.list'
wget https://packages.osrfoundation.org/gazebo.key -O - | sudo apt-key add -
```

Install packages:
```sh
sudo apt update
sudo apt install libgz-transport12
```


### Compilation

The build folder needs to be created and cmake needs to be launched to generate the build files neccessary.

```
mkdir build
cd build
cmake ..
make
```

The created binary can be found under `build/cpp_proxy`.

## Launch
The cpp-proxy needs to be run on the **host system outside of the TrentOS docker container**.

Example Launch
```sh
./cpp_proxy 172.17.0.2:5555 -v /navsat
```

The docker ip address of the TrentOS test docker container can vary between systems and needs to be adjusted accordingly.

If gazebo runs on another machine in the same network, some environment variables need to be set in order to be able to retrieve the topics.

Simulation environment:
```sh
export GZ_PARTITION=relay
export GZ_IP=<ip of this host in the local network>
```

cpp_proxy environment:
```sh
export GZ_PARTITION=relay
```

for convenience the `run_proxy.sh` script can be used like the binary which automatically sets up the environment.

# Drone Sim Demo SITL

This demo consists of the following components:

- Gazebo Simulator (Simulates a Quadcopter) | runs on the host system
- Px4 Flight Controler | Runs on the host system
- Mavp2p (Mavlink Proxy)  | Runs on the host system
- SerialFilter (Filters and forwards mavlink messages) | Qemu(Trentos(SerialFilter)) -> trentos component
- SimCoupler (Forwards sensor data) | Qemu(Trentos(SimCoupler)) -> trentos component
- LinuxVM (Buildroot linux) | Qemu(Trentos(CamkesVMM(Linux))) -> guest vm running on trentos
- DummyAi (small mavlink mission script) | Qemu(Trentos(CamkesVMM(Linux(DummyAi)))) -> runs on the linux guest system

These components together allow for the control of a simulated drone. 

## Installation of the components

### Gazebo
(Gazebo instructions)[https://docs.px4.io/main/en/sim_gazebo_gz/]

The gazebo repository must be added to apt and the gz-garden binary be installed.

```sh
sudo wget https://packages.osrfoundation.org/gazebo.gpg -O /usr/share/keyrings/pkgs-osrf-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/pkgs-osrf-archive-keyring.gpg] http://packages.osrfoundation.org/gazebo/ubuntu-stable $(lsb_release -cs) main" | sudo tee /etc/apt/sources.list.d/gazebo-stable.list > /dev/null
sudo apt-get update
sudo apt-get install gz-garden
```

### PX4-Autopilot
(PX4 instructions)[https://docs.px4.io/main/en/dev_setup/building_px4.html]

The PX4-Autopilot repository must be cloned. The project is compiled when launched.

```sh
git clone https://github.com/PX4/PX4-Autopilot.git --recursive
```

### Mavp2p
Download and install mavp2p
(Current release)[https://github.com/bluenviron/mavp2p/releases/latest/]

```sh
wget -qO- https://github.com/bluenviron/mavp2p/releases/download/v1.0.0/mavp2p_v1.0.0_linux_amd64.tar.gz | sudo tar -xz -C /usr/local/bin
```

## Launching of the demonstrator:
Three different shell sessions should be started and the following command be executed in order.

### Mavp2p Proxy
This proxy is required to tunnel the connection from the docker network to the local network.
Otherwise a connection between the SerialFilter and Px4 cannot be established without applying new iptables rules.

```sh
mavp2p udps:127.0.0.1:14550 udpc:172.17.0.2:7000
```


### Px4 & Gazebo
The gazebo instance is started and configured by the px4 flight controler.

```sh
cd <px4 git repo>
make px4_sitl gz_x500
```

### Demo vm drone sim
This launches the trentos demo. This demo includes the `SerialFilter`, `SimCoupler` components as well as the linux guest system.

```sh
cd <trentos sdk folder>
src/build.sh build-and-test demo_vm_drone_sim
```


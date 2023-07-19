# Drone mission

The drone mission script is targeted to run on the linux vm inside TRENTOS.
Therefore the file needs to be crosscompiled for the target platform using the buildroot toolchain.

## Dependencies

MAVSDK C++ is required to compile the script. 

Either the binary packages can be installed or the mavsdk git repository can be cloned and installed locally.

### Local install
Set the compiler environment paths to the buildroot toolchain gcc/g++
```sh
export CC=/home/felsch01/drone_project/buildroot-vm/zynqmp-zcu102-output/host/usr/bin/aarch64-linux-gcc
export CXX=/home/felsch01/drone_project/buildroot-vm/zynqmp-zcu102-output/host/usr/bin/aarch64-linux-g++
```

Download and build MAVSDK
For more detailed information visit the [MAVSDK WIKI](https://mavsdk.mavlink.io/main/en/cpp/guide/build.html)
```sh
git clone https://github.com/mavlink/MAVSDK.git
cd MAVSDK
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=install -Bbuild/default -H.
cmake --build build/default --target install
```


## Compile the mission script

To compile the mission script, open a terminal in this folder.
```sh
cmake -DCMAKE_PREFIX_PATH=$(pwd)/../../install -Bbuild -H.
cmake --build build -j8
```

## Install script
The script needs to be moved into the overlay file structure to be accessable in the linux vm.
```sh
cp build/drone_mission ../../overlay_files/init_scripts/drone_mission
```

## Credit

For the json parsing the json library from Nils Lohmann is used. 
[json github](https://github.com/nlohmann/json)


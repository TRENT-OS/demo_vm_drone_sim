#
# Copyright (C) 2022-2023, HENSOLDT Cyber GmbH
#

cmake_minimum_required(VERSION 3.8.2)

project(demo_vm_drone_sim C)

#-------------------------------------------------------------------------------
# use the SDK
find_package(os-sdk REQUIRED)
os_sdk_set_defaults()
set(KernelRootCNodeSizeBits 16 CACHE STRING "")
set(CapDLLoaderMaxObjects 7500 CACHE STRING "" FORCE)
set(LibUtilsDefaultZfLogLevel 2 CACHE STRING "")


# Kernel settings
set(KernelArmHypervisorSupport ON CACHE BOOL "" FORCE)
set(KernelArmVtimerUpdateVOffset OFF CACHE BOOL "" FORCE)
set(KernelArmDisableWFIWFETraps ON CACHE BOOL "" FORCE)

# VMM Feature Settings
set(LibUSB OFF CACHE BOOL "" FORCE)
set(VmPCISupport ON CACHE BOOL "" FORCE)
set(VmVirtioConsole OFF CACHE BOOL "" FORCE)
set(VmVirtioNetVirtqueue ON CACHE BOOL "" FORCE)

include("plat/${PLATFORM}/plat.cmake")

os_sdk_setup(CONFIG_FILE "system_config.h" CONFIG_PROJECT "system_config")
os_sdk_import_camkes_vm()
os_sdk_import_from_global_components(
    "components/FileServer"
    "components/VirtQueue"
)

CAmkESAddCPPInclude("plat/${PLATFORM}")


#-------------------------------------------------------------------------------
# config
AddCamkesCPPFlag(cpp_flags CONFIG_VARS VmEmmc2NoDMA)

if(NOT KernelPlatformQEMUArmVirt)
    AddCamkesCPPFlag(cpp_flags CONFIG_VARS VmVUSB)
endif()

if(KernelPlatformTK1)
    AddCamkesCPPFlag(cpp_flags CONFIG_VARS Tk1DeviceFwd Tk1Insecure)
endif()


#-------------------------------------------------------------------------------
# VM Images
include("${CAMKES_VM_LINUX_HELPERS_PATH}")
include("${CMAKE_CURRENT_SOURCE_DIR}/camkes-vm-linux/vm-linux-helpers.cmake")


# images for the selected platform
set(VM_IMG_LINUX "${CMAKE_CURRENT_SOURCE_DIR}/vm-images/${PLATFORM}/linux")
set(VM_IMG_ROOTFS "${CMAKE_CURRENT_SOURCE_DIR}/vm-images/${PLATFORM}/rootfs.cpio.gz")


# patch root-fs
AddOverlayDirToRootfs(
    overlay
    "${CMAKE_CURRENT_SOURCE_DIR}/vm-images/${PLATFORM}/rootfs.cpio.gz"
    "buildroot"
    "rootfs_install"
    VM_IMG_ROOTFS_PATCHED
    rootfs_target
    GZIP
)
AddFileToOverlayDir(
    "interfaces"
    "${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/network_interfaces/interfaces"
    "etc/network"
    overlay
)
AddFileToOverlayDir(
    "S90ping_test"
    "${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/init_scripts/ping_test"
    "etc/init.d"
    overlay
)

AddFileToOverlayDir(
    "fly_mission"
    "${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/init_scripts/fly_mission"
    "etc/"
    overlay
)
AddFileToOverlayDir(
    "takeoff_and_land"
    "${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/init_scripts/takeoff_and_land"
    "etc/"
    overlay
)

AddFileToOverlayDir(
    "libmavsdk.so.1"
    "${CMAKE_CURRENT_SOURCE_DIR}/overlay_files/lib/libmavsdk.so.1"
    "lib/"
    overlay
)


# Since this needs the file VM_IMG_ROOTFS_PATCHED anyway, there is an implicit
# dependency already. Thus, there is no need to explicitly add a dependency on
# the target 'rootfs_target' here.
DefineCAmkESVMFileServer(
    INSTANCE "fileServer"
    FILES
        "linux:${VM_IMG_LINUX}"
        "linux-initrd:${VM_IMG_ROOTFS_PATCHED}"
)

#-------------------------------------------------------------------------------
# Add the nic driver for virtio
include("components/NIC_virtio/NIC_virtio.cmake")

NIC_virtio_DeclareCAmkESComponent(
    VirtioNIC
)

TimeServer_DeclareCAmkESComponent(
	TimeServer
)

NetworkStack_PicoTcp_DeclareCAmkESComponent(
    NetworkStack_GCS
    C_FLAGS
        -DNetworkStack_PicoTcp_USE_HARDCODED_IPADDR
        -DDEV_ADDR=GCS_TRENTOS_ADDR
        -DGATEWAY_ADDR=GCS_GATEWAY_ADDR
        -DSUBNET_MASK=ETH_SUBNET_MASK
        -DOS_NETWORK_MAXIMUM_SOCKET_NO=2
)

NetworkStack_PicoTcp_DeclareCAmkESComponent(
    NetworkStack_PX4
    C_FLAGS
        -DNetworkStack_PicoTcp_USE_HARDCODED_IPADDR
        -DDEV_ADDR=PX4_TRENTOS_ADDR
        -DGATEWAY_ADDR=PX4_GATEWAY_ADDR
        -DSUBNET_MASK=ETH_SUBNET_MASK
        -DOS_NETWORK_MAXIMUM_SOCKET_NO=2
)

DeclareCAmkESComponent(
    SerialFilter
    INCLUDES
        libs/mavgenlib
        libs/mavgenlib/common
    SOURCES
        components/SerialFilter/SerialFilter.c
        libs/mavlink_filter/mavlink_filter.c
        libs/mavlink_filter/geofence.c
    C_FLAGS
        -Wall
        -Werror
        -Wno-address-of-packed-member # ignore MAVLink errors according to https://mavlink.io/en/mavgen_c/#build-warnings 
        -DOS_NETWORK_MAXIMUM_SOCKET_NO=1
    LIBS
        system_config
        lib_debug
        os_core_api
        os_filesystem
		os_socket_client
)

DeclareCAmkESComponent(
    SimCoupler
    SOURCES
        components/SimCoupler/SimCoupler.c
    C_FLAGS
        -Wall
        -Werror
        -DOS_NETWORK_MAXIMUM_SOCKET_NO=1
    LIBS
        system_config
        lib_debug
        os_core_api
        os_filesystem
		os_socket_client
)

NIC_ChanMux_DeclareCAmkESComponent(
    ChanMuxNIC
    CHANMUX_CHANNEL_NIC_CTRL
    CHANMUX_CHANNEL_NIC_DATA
)

ChanMux_UART_DeclareCAmkESComponents(
    ChanMux_UART
    components/ChanMux/ChanMux_config.c
    system_config
)

#-------------------------------------------------------------------------------
os_sdk_create_CAmkES_system("vm_drone_sim.camkes")
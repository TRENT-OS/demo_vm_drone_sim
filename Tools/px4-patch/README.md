# PX4 Patch file

This patchfile is intended to be applied onto the PX4 instance used for running the demonstrator.

It contains three changes:
- Introduction of WGS84 coordinate system to `Tools/simulation/gz/wolrd/default.sdf`
- Introduction of the navsat sensor/plugin to the drone `Tools/simulation/gz/model/model.sdf`
- Adding `./run` script in the root directory for easy launching with the correct environment variables set

## Application

```sh
cd PX4-Autopilot
git apply <path to patchfile>/px4.patch
```

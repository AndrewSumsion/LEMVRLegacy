# LEMVR
Pronounced "lemur", **L**EMVR's **EM**ulated **V**irtual **R**eality

The goal of this project is to bridge the gap between Android-based virtual reality (Oculus Quest, smartphone VR) and desktop virtual reality. It is a modified version of the Android Studio emulator that submits frames produced by the emulator to OpenVR to be viewed on a desktop VR headset like an Oculus Rift or HTC Vive.

The Android emulator is a perfect starting point for this project because of its OpenGL ES and Vulkan emulation for the graphically intensive nature of VR applications.

## Current features:
 - Submits side-by-side frames from the emulator to OpenVR. Meaning that the left half of the display is submitted to the left eye, and the right half to the right eye.

## Planned features:
 - TCP server that sends rendering and tracking data from OpenVR to the emulator.
 - Custom implementation of the Oculus Mobile SDK to run apps made for the Oculus Quest/Go.
 - IMU emulation based on tracking data from OpenVR (for smartphone VR like Google Cardboard).
 - User-friendly launcher for VR apps installed on the emulator

Note that most if not all VR apps will require some kind of ARM translation like libhoudini or the official Android 11 images.

## Building instructions:
Follow the instructions found [here](https://github.com/AndrewSumsion/LEMVR/blob/lemvr-master/android/docs/LINUX-DEV.md) (Linux) or [here](https://github.com/AndrewSumsion/LEMVR/blob/lemvr-master/android/docs/WINDOWS-DEV.md) (Windows).

Stop before you run `android/rebuild`, and follow these instructions.

Remove the default android emulator directory:
```
$ cd ..
$ rm -r qemu
```

Clone this repository in its place:
```
$ git clone https://github.com/AndrewSumsion/LEMVR.git qemu --recursive
```

Build:
```
$ cd qemu

# Linux
$ android/rebuild.sh

# Windows
$ android\rebuild.cmd
```

The resulting emulator can be found in `objs/distribution/emulator`. Replace the `emulator` folder in your Android SDK install location with this folder. Create a horizontal AVD for testing with Android Studio and run the emulator from the command line:
```
$ ./emulator -avd <avd name> -no-snapshot
```

## Known Issues:
 - Because of a driver issue on AMD graphics cards on Windows, the OpenGL texture format `RGBA8` is forced. This breaks the snapshot functionality of the emulator, and it must be run with the `-no-snapshot` argument.
 - The emulator must be run with the Steam runtime on Linux. ex: `~/.steam/steam/ubuntu12_32/steam-runtime/run.sh ./emulator ...`
 - The emulator sometimes hangs on shutdown, requiring a force-kill.
 - When force-killing on Windows, both `qemu-system-x86_64.exe` and `adb.exe` must be killed to free up file resources.
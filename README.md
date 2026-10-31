# Streaming audio support for Pothos

The audio toolkit gives two blocks: `/audio/source` reads an audio input
device (a microphone or a line input) into a sample stream, and
`/audio/sink` writes a sample stream to an audio output device (the
speakers). `/devices/audio/info` lists the audio devices.

This branch (`pothos2`) builds against the modernized core
(https://github.com/markvin/PothosCore, branch `pothos2`). The device
input and output come from miniaudio (https://miniaud.io), not from
PortAudio.

## Documentation

* https://github.com/pothosware/PothosAudio/wiki

## Dependencies

- an installed Pothos core (`find_package(Pothos)`);
- nlohmann_json 3.11 or higher (`find_package`: the system package, for
  example `nlohmann-json` on Arch Linux, or Conan with `conanfile.txt`);
- for the tests: PothosBlocks in the prefix of the core;
- CMake 3.25 or higher and C and C++23 compilers.

miniaudio is a copy in `third_party/miniaudio` (refer to its
`README.md` for the version and the update steps). The build needs no
audio development packages. On Linux, miniaudio loads ALSA, PulseAudio
or JACK at run time; PipeWire works through its PulseAudio or ALSA
compatibility. macOS uses Core Audio, and Windows uses WASAPI.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=<core prefix>
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build
```

`<core prefix>` is the installation prefix of the Pothos core. Do not set
it when the core is installed in a default location of the system. The
default install prefix is the prefix of the core. When you give
`CMAKE_INSTALL_PREFIX`, set `POTHOS_PLUGIN_PATH` to
`<prefix>/lib/Pothos/modules<ABI version>`.

## Devices

- An empty device name selects the default device of the system.
- A number selects a device by its index in the input list (source) or
  in the output list (sink).
- Other text selects the device with that name. The names come from
  miniaudio: a topology that has a PortAudio device name (for example
  `hw:0,0` or `pulse`) gives an error "No matching device". Select the
  device again in PothosFlow.

The device converts the sample format, the channel count and the sample
rate. Thus a rate or a channel count that the hardware does not support
works (for example 44.1 kHz on a 48 kHz device). The label `rxRate` of
the source gives the rate of the stream.

The environment variable `POTHOS_AUDIO_BACKEND` selects the miniaudio
backend, for example `pulseaudio`, `alsa`, `jack`, `coreaudio`, `wasapi`
or `null` (a virtual device without hardware, for the tests). Without the
variable, miniaudio selects the backend in its default order, without
the null backend.

## Tests

The test `audio.stage` installs the module into `build/stage`. Then
`audio.selftests` runs each self-test (`/audio/tests/...`) with the
PothosUtil of the core and `POTHOS_AUDIO_BACKEND=null`. The tests need no
sound card and no sound server. Install PothosBlocks into the prefix of
the core before you run the tests.

## Licensing information

Use, modification and distribution is subject to the Boost Software
License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
http://www.boost.org/LICENSE_1_0.txt)

miniaudio (`third_party/miniaudio`) is public domain or MIT No
Attribution; refer to `third_party/miniaudio/LICENSE`.

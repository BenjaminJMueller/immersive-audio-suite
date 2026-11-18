# Raumklang

[Deutsch](README.md) · English

Raumklang is a free tool for spatial audio on macOS and Windows. It places sound sources freely in space on any number of speakers: from a stereo pair through rings, domes and 7.1.4 up to a *donut*, where two speaker rings face each other. It includes reverb, a headphone preview and two DAW plugins. An audio interface is all you need: the app talks to it directly, no extra routing software required.

> **Early version (0.1).** The audio processing is extensively tested automatically, but hardly on real speaker setups yet.

## Workflow

1. **Speakers.** Pick a preset (stereo, quad, 5.1, 7.1.4, ring, dome, cube, donut) or place and drag speakers freely. Raumklang checks the layout and explains in plain words which panning method it uses.
2. **Sources.** Each source is an input of the audio interface or a built-in test signal. Drag sources in the top view or set azimuth, elevation and distance.
3. **Room.** Reverb with presets and controls for decay, size, damping and pre-delay.
4. **Output.** Audio device, sample rate, buffer, master level, protection limiter, headphone preview and a speaker test.

## In the DAW

- **Raumklang Source** goes on each track and outputs one channel per speaker.
- **Raumklang Room** goes on the speaker bus: layout, per-speaker delay and gain, headphone preview, limiter.

App and plugins share the speaker setup through `Raumklang/Lautsprecher-Setup.json` in the user folder.

## Panning

VBAP (2D/3D) for enclosing layouts, DBAP with speaker directivity for layouts without a central listening spot (this makes the donut work), and 3rd-order Ambisonics with an AllRAD decoder. The method is chosen automatically.

## Stability

The audio thread never allocates, locks or waits. Every push runs unit, realtime-safety, stress and fuzz tests, the same tests under ASan/UBSan/TSan, a plugin host stress test and pluginval (strictness 10) on macOS, Windows and Linux.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

See the [roadmap](docs/ROADMAP.md) for what comes next.

## License

[GNU AGPLv3](LICENSE.md). Built on [JUCE](https://juce.com), which is free for open-source projects under AGPLv3.

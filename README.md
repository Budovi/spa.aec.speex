# spa-aec-speex

A [PipeWire SPA](https://docs.pipewire.org/page_spa.html) plugin that performs acoustic echo cancellation (AEC) using the [SpeexDSP](https://gitlab.xiph.org/xiph/speexdsp) library. It removes speaker output from microphone input in real time, making it suitable for use in calls and conferencing setups.

## Motivation & expectations

I wrote this pipewire plugin after I was not happy with the WebRTC one provided with PipeWire. I've tried an older version of [Easy Effects](https://github.com/wwmm/easyeffects) that featured Speex AEC, and I liked the results quite a bit more. I wanted to experiment with the parameters, and having the ability to run it in a standalone manner, with just a JSON configuration felt like the simplest option.

From my testing, the linear echo cancellation (the "preprocessor options" turned off) works quite well on its own, while maintaining the input quality. The downside is that the background audio is simply suppressed and remains audible, and i.e. any background music remains recognizable. Enabling the preprocessor features makes the cancellation significantly better (albeit not perfect) at the cost of severe voice audio quality degradation.

Note that the "preprocessor" is actually run on the output signal, after the linear echo suppression occurs. The naming is most likely in line with the original library purpose (voice codec implementation), where the preprocessor would be executed before encoding.

It could be worth trying to combine the linear Speex AEC with a different noise suppression solution. The Speex DSP library is, in general, quite dated. I also don't mind mainlining this plugin, but I don't feel like there are enough people that would be actually interested in it.

### ⚠️ Disclaimer

An AI aided the creation of this project, mostly with this documentation and the CMake build system. Without it, this small experiment wouldn't become public. While I did my best reviewing and testing it, there is always a chance I've missed something.

The code was inspired by the two existing AEC plugins in the PipeWire (WebRTC and the "null" one).

## Known issues

* The monitor mode makes the "passive" mode ineffective, e.g. the AEC will run regardless of whether there is an app actually capturing the microphone audio. The monitor outputs seem to be enough to activate the processing.
* Voice activity detection options were kept in the code, but the Speex authors discourage its use. There is a lack of proper documentation about the issue, and I haven't experimented with it.
* To remove echo from multiple microphone streams (e.g. when using a stereo microphone) with preprocessing enabled you need to run multiple instances of the plugin.

## Dependencies

- [PipeWire](https://docs.pipewire.org/) ≥ 0.3 — for the SPA plugin interface (`libpipewire-0.3`)
- [SpeexDSP](https://gitlab.xiph.org/xiph/speexdsp) — provides echo cancellation, residual echo suppression, noise suppression, and dereverberation (`speexdsp`)

### Relevant documentation

| Topic | Link |
|-------|------|
| SPA plugin API | [Simple Plugin API — PipeWire docs](https://docs.pipewire.org/page_spa_plugins.html) |
| SPA audio AEC interface | [`spa/interfaces/audio/aec.h`](https://gitlab.freedesktop.org/pipewire/pipewire/-/blob/master/spa/include/spa/interfaces/audio/aec.h) |
| PipeWire modules and filters | [PipeWire module docs](https://docs.pipewire.org/page_module_echo_cancel.html) |
| SpeexDSP API reference | [Speex API — echo cancellation](https://www.speex.org/docs/api/speex-api-reference/group__SpeexEcho.html) |
| SpeexDSP preprocessor API | [Speex API — preprocessing](https://www.speex.org/docs/api/speex-api-reference/group__SpeexPreprocess.html) |

## Building

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Install (adjust `CMAKE_INSTALL_PREFIX` and `CMAKE_INSTALL_LIBDIR` to match your system):

```sh
# Fedora / RHEL / openSUSE (lib64)
cmake --install build --prefix /usr

# Debian / Ubuntu (lib or lib/x86_64-linux-gnu)
cmake -B build -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix /usr
```

The plugin is installed to `${prefix}/${libdir}/spa-0.2/aec/libspa-aec-speex.so`
(e.g. `/usr/lib64/spa-0.2/aec/libspa-aec-speex.so` on Fedora with `--prefix /usr`).

## Configuration

The plugin is loaded by PipeWire's echo-cancel module. See `60-aec-speex.conf` for a complete example. The following parameters can be passed via `aec.args`:

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `speex.frame_length` | uint (ms) | `10` | Processing frame length |
| `speex.filter_length` | uint (ms) | `100` | Echo tail length (filter history) |
| `speex.filter_delay` | uint (ms) | `20` | Estimated latency from speakers to microphone |
| `speex.preprocess` | bool | `false` | Enable the SpeexDSP preprocessor (requires mono recording) |
| `speex.preprocess.echo_suppress` | int (dB) | -40 *(library default)* | Residual echo suppression ceiling |
| `speex.preprocess.denoise` | bool | `false` | Enable noise suppression |
| `speex.preprocess.noise_suppress` | int (dB) | -15 *(library default)* | Noise suppression ceiling |
| `speex.preprocess.dereverb` | bool | `false` | Enable dereverberation |

The configuration needs to be placed to the PipeWire's configuration folder, typically `~/.config/pipewire/pipewire.conf.d/`. Don't forget to restart the daemon using `systemctl --user restart pipewire`. Inspect the output via `journalctl --user -u pipewire` if you encounter problems, and expect your audio to glitch out when restarting the PipeWire. I recommend [Helvum](https://gitlab.freedesktop.org/pipewire/helvum) patchbay for wiring inspection.

## License

MIT — see [LICENSE](LICENSE).

# spa-aec-speex

A [PipeWire SPA](https://docs.pipewire.org/page_spa.html) plugin that performs acoustic echo cancellation (AEC) using the [SpeexDSP](https://gitlab.xiph.org/xiph/speexdsp) library. It removes speaker output from microphone input in real time, making it suitable for use in calls and conferencing setups.

## Dependencies

- [PipeWire](https://docs.pipewire.org/) ≥ 0.3 — provides the SPA plugin interface and headers (`libpipewire-0.3`)
- [SpeexDSP](https://gitlab.xiph.org/xiph/speexdsp) — provides echo cancellation and audio preprocessing (`speexdsp`)

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
| `speex.preprocess.echo_suppress` | int (dB) | *(library default)* | Residual echo suppression ceiling |
| `speex.preprocess.denoise` | bool | `false` | Enable noise suppression |
| `speex.preprocess.noise_suppress` | int (dB) | *(library default)* | Noise suppression ceiling |
| `speex.preprocess.dereverb` | bool | `true` | Enable dereverberation |

## License

MIT — see [LICENSE](LICENSE).

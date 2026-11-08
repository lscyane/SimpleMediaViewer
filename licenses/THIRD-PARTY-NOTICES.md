# Third-Party Notices

This directory contains license and copyright notices for third-party components referenced by the current `SimpleMediaViewer.Native` x64 Release configuration. The vcpkg notices below were copied from the installed vcpkg package's `share/<port>/copyright` file without changing their contents. Versions reflect the inspected local installation; update this file and the corresponding notices whenever dependency versions or the shipped runtime change.

## vcpkg runtime dependencies

| Component | Inspected version | Notice file |
| --- | ---: | --- |
| SDL3 | 3.4.12 | [SDL3 copyright](third-party/SDL3-3.4.12/copyright.txt) |
| SDL3_image | 3.4.4 | [SDL3_image copyright](third-party/SDL3_image-3.4.4/copyright.txt) |
| libavif | 1.4.2 | [libavif copyright](third-party/libavif-1.4.2/copyright.txt) |
| AOM | 3.13.3 | [AOM copyright](third-party/aom-3.13.3/copyright.txt) |
| dav1d | 1.5.3 | [dav1d copyright](third-party/dav1d-1.5.3/copyright.txt) |
| libjpeg-turbo | 3.2.0 | [libjpeg-turbo copyright](third-party/libjpeg-turbo-3.2.0/copyright.txt) |
| liblzma / XZ Utils | 5.8.3 | [XZ Utils licensing](third-party/liblzma-5.8.3/copyright.txt) |
| libpng | 1.6.58 | [libpng copyright and license](third-party/libpng-1.6.58/copyright.txt) |
| libwebp | 1.6.0 | [libwebp copying terms](third-party/libwebp-1.6.0/copyright.txt) |
| libyuv | 1916 | [libyuv copyright](third-party/libyuv-1916/copyright.txt) |
| LibTIFF | 4.7.2 | [LibTIFF license](third-party/tiff-4.7.2/copyright.txt) |
| zlib | 1.3.2 | [zlib copyright and license](third-party/zlib-1.3.2/copyright.txt) |

These are the runtime packages observed in the local vcpkg installation and Release output. Build-only vcpkg tools are not listed as shipped components. Re-check the actual final package before each release.

## VideoLAN.LibVLC.Windows

- NuGet package: `VideoLAN.LibVLC.Windows` 3.0.24
- License expression declared by the package metadata: `LGPL-2.1-or-later`
- License text: [GNU LGPL 2.1](third-party/VideoLAN.LibVLC.Windows-3.0.24/LGPL-2.1.txt)
- The current project build copies `libvlc.dll`, `libvlccore.dll`, and the package's `plugins`, `hrtfs`, and `lua` directories.

The NuGet package's license expression does not by itself establish the license of every individual file in those copied directories. The VLC plugin, Lua, and other bundled file inventory has not been audited component by component. Before redistributing the resulting application, verify the license and any additional conditions for every shipped VLC component, and provide the corresponding source or other materials required by the applicable license terms. Do not treat this notice as confirmation that the VLC runtime bundle is fully cleared for redistribution.

## Scope

These notices describe the locally inspected dependency configuration, not every possible build configuration. They do not replace the license terms accompanying each component. The application project's own license is separate; its Apache-2.0 license file has not yet been added.

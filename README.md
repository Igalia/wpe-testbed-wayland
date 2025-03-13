# wpe-testbed-wayland

## Description

An application that simulates WPE WebKit rendering/composition using OpenGL / Wayland / GBM / EGL.

## Prerequisites (imx8mp specific)

Be sure to activate `export ETNA_MESA_DEBUG=linear_pe`.
Now you can run the benchmarks in the scripts/ folders.

## Prerequisites (imx6?? specific)

TODO: Ask Pablo.

## Build

Build using cmake - should produce a `wpe-testbed-wayland` binary.

## Using it

The testbed application contains many flags, to simulate the way WPE is rendering. Many different combinations can be tested.
Get inspiration from the test scripts in the `scripts` subdirectory.

To be close to the current WPE way of rendering be sure to pass these options: `--linear-filter`, `--depth`, `--blend`, `--explicit-sync`, `--rbo`, `--fences`, `--opaque`.
To test the "new way" of texture uploading, additionally pass `--dmabuf-tiles`, `--tile-update-method mmap`, `--tile-buffer-modifier vivante-super-tiled` and `--neon`.

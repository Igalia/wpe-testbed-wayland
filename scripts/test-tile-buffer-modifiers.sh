#!/usr/bin/env bash
OPTIONS="--tile-width 1920 --tile-height 1080 --tiles 1 --opaque --fences --rbo --tile-update-method mmap --dmabuf-tiles --frames 1000 --no-animate --unbounded"

set -x

# Script to test the performance of the three different tile buffer modifiers
# Obtained results on imx8mp, using scarthgap, before overclocking+GPU was fixed

# NOTE: If you want to compare the netdata statistics such as gpu.load, memory.read
# or memory.write bandwidth you need to disable unbounded mode, otherwise you'd see
# more bandwidth used in _faster_ methods, just because they render quicker.

# Rendered  1000 frames in 10.012 sec (99.881 fps)
# Rendered  1000 frames in 7.099 sec (140.863 fps)
sleep 0; wpe-testbed-wayland ${OPTIONS[@]} --tile-buffer-modifier linear
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --tile-buffer-modifier linear --neon

# Rendered  1000 frames in 15.249 sec (65.577 fps)
# Rendered  1000 frames in 8.852 sec (112.975 fps)
sleep 8; wpe-testbed-wayland ${OPTIONS[@]} --tile-buffer-modifier vivante-tiled
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --tile-buffer-modifier vivante-tiled --neon

# Rendered  1000 frames in 20.741 sec (48.214 fps)
# Rendered  1000 frames in 13.357 sec (74.869 fps)
sleep 8; wpe-testbed-wayland ${OPTIONS[@]} --tile-buffer-modifier vivante-super-tiled
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --tile-buffer-modifier vivante-super-tiled --neon

#!/usr/bin/env bash
OPTIONS="--tile-width 1920 --tile-height 1080 --tiles 1 --opaque --fences --rbo --tile-update-method mmap --dmabuf-tiles --frames 1000 --no-animate --unbounded"

set -x

# Script to test the performance of the three different tile buffer modifiers
# Obtained results on imx8mp, using scarthgap, before overclocking+GPU was fixed

# We need linear window buffer on imx8mp to use direct scanout. That is not the
# case on the imx6 platforms, where the display controller can scanout non-linear
# texture formats, such as Vivante super tiled. Therefore on imx6 is should be
# interesting to use super tiled texture formats for both window & tile buffers.

# Rendered  1000 frames in 7.097 sec (140.912 fps)
# Rendered  1000 frames in 6.796 sec (147.147 fps)
# Rendered  1000 frames in 6.260 sec (159.733 fps)
sleep 0; wpe-testbed-wayland ${OPTIONS[@]} --window-buffer-modifier linear              --tile-buffer-modifier linear --neon
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --window-buffer-modifier vivante-tiled       --tile-buffer-modifier linear --neon
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --window-buffer-modifier vivante-super-tiled --tile-buffer-modifier linear --neon

# Rendered  1000 frames in 8.915 sec (112.165 fps)
# Rendered  1000 frames in 8.530 sec (117.234 fps)
# Rendered  1000 frames in 8.060 sec (124.067 fps)
sleep 8; wpe-testbed-wayland ${OPTIONS[@]} --window-buffer-modifier linear              --tile-buffer-modifier vivante-tiled --neon
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --window-buffer-modifier vivante-tiled       --tile-buffer-modifier vivante-tiled --neon
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --window-buffer-modifier vivante-super-tiled --tile-buffer-modifier vivante-tiled --neon

# Rendered  1000 frames in 13.443 sec (74.389 fps)
# Rendered  1000 frames in 13.243 sec (75.514 fps)
# Rendered  1000 frames in 12.791 sec (78.179 fps)
sleep 8; wpe-testbed-wayland ${OPTIONS[@]} --window-buffer-modifier linear              --tile-buffer-modifier vivante-super-tiled --neon
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --window-buffer-modifier vivante-tiled       --tile-buffer-modifier vivante-super-tiled --neon
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --window-buffer-modifier vivante-super-tiled --tile-buffer-modifier vivante-super-tiled --neon

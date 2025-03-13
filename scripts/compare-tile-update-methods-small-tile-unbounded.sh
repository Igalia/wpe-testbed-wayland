#!/usr/bin/env bash
OPTIONS="--tile-width 512 --tile-height 512 --tiles 1 --opaque --fences --rbo --frames 10000 --no-animate --unbounded"

set -x

# Script to test the performance of the three different tile update methods (gl / gbm / mmap)
# Obtained results on imx8mp, using scarthgap, before overclocking+GPU was fixed

# 4. Unbounded rendering, 512x512 tiles, 1 tile, three update methods, fences activated
# Purpose: Don't use the Wayland rendering feedback, just upload-composite-commit as fast as possible.
#          This should verify the trend seen in standard rendering: GPU.load is 100% for GL, smaller for GBM, and only 50% for mmap().
# Expected FPS:
#   GL: Rendered 10000 frames in 22.170 sec (451.063 fps)
#  GBM: Rendered 10000 frames in 14.533 sec (688.110 fps)
# MMAP: Rendered 10000 frames in 14.394 sec (694.722 fps)

sleep 0; wpe-testbed-wayland ${OPTIONS[@]} --tile-update-method gl
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --tile-update-method gbm --dmabuf-tiles
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --tile-update-method mmap --dmabuf-tiles

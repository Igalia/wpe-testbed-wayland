#!/usr/bin/env bash
OPTIONS="--tile-width 512 --tile-height 512 --tiles 1 --opaque --rbo --frames 1000 --no-animate"

set -x

# Script to test the performance of the three different tile update methods (gl / gbm / mmap)
# Obtained results on imx8mp, using scarthgap, before overclocking+GPU was fixed

# 1. Standard rendering, 512x512 tiles, 1 tile, three update methods, no fencing
# Purpose: Verify that GL uses more bandwidth / higher GPU load than GBM/mmap, and that mmap is better than gbm.
# Expected FPS:
#   GL: Rendered  1000 frames in 16.651 sec (60.056 fps)
#  GBM: Rendered  1000 frames in 16.651 sec (60.056 fps)
# MMAP: Rendered  1000 frames in 16.651 sec (60.057 fps)

sleep 0; wpe-testbed-wayland ${OPTIONS[@]} --tile-update-method gl
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --tile-update-method gbm --dmabuf-tiles
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --tile-update-method mmap --dmabuf-tiles

# 2. Standard rendering, 512x512 tiles, 1 tile, three update methods, fences activated
# Purpose: Verify that GL uses more bandwidth / higher GPU load than GBM/mmap, and that mmap is better than gbm.
# Expected FPS:
#   GL: Rendered  1000 frames in 16.652 sec (60.052 fps)
#  GBM: Rendered  1000 frames in 16.652 sec (60.054 fps)
# MMAP: Rendered  1000 frames in 16.652 sec (60.054 fps)

sleep 8; wpe-testbed-wayland ${OPTIONS[@]} --fences --tile-update-method gl
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --fences --tile-update-method gbm --dmabuf-tiles
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --fences --tile-update-method mmap --dmabuf-tiles

# 3. Standard rendering, 512x512 tiles, 1 tile, three update methods, fences activated, explicit sync activated
# Purpose: Verify that GL uses more bandwidth / higher GPU load than GBM/mmap, and that mmap is better than gbm.
# Expected FPS:
#   GL: Rendered  1000 frames in 16.652 sec (60.051 fps)
#  GBM: Rendered  1000 frames in 16.650 sec (60.061 fps)
# MMAP: Rendered  1000 frames in 16.652 sec (60.054 fps)

sleep 8; wpe-testbed-wayland ${OPTIONS[@]} --fences --explicit-sync --tile-update-method gl
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --fences --explicit-sync --tile-update-method gbm --dmabuf-tiles
sleep 4; wpe-testbed-wayland ${OPTIONS[@]} --fences --explicit-sync --tile-update-method mmap --dmabuf-tiles

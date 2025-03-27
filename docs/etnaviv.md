# etnaviv debugging

We usually use tools such as `apitrace` to follow the EGL/GL execution during rendering,
to get a high-level picture of what commands are sent to the GPU from GL API level.

With etnaviv we can dig deeper and actually examine the command stream sent to the GPU
from user-land (Gallium frontend) to kernel side.

## Patching etnaviv

Apply the following patch to your local Mesa source tree:

```
diff --git a/src/etnaviv/drm/etnaviv_cmd_stream.c b/src/etnaviv/drm/etnaviv_cmd_stream.c
index 93a7a87..5ad75d6 100644
--- a/src/etnaviv/drm/etnaviv_cmd_stream.c
+++ b/src/etnaviv/drm/etnaviv_cmd_stream.c
@@ -219,6 +219,21 @@ void etna_cmd_stream_flush(struct etna_cmd_stream *stream, int in_fence_fd,
                .stream_size = stream->offset * 4, /* in bytes */
        };
 
+   if (getenv("ETNA_DUMP")) {
+      if (stream->offset && stream->offset != priv->offset_end_of_context_init) {
+         static int i;
+         char name[1024] = {0};
+         snprintf(name, sizeof(name), "/tmp/cmd_%u.buf", i++);
+
+         FILE *pFile = fopen(name, "wb");
+
+         if (pFile){
+            fwrite(stream->buffer, stream->offset * 4, 1, pFile);
+            fclose(pFile);
+         }
+      }
+   }
+
        if (in_fence_fd != -1) {
                req.flags |= ETNA_SUBMIT_FENCE_FD_IN | ETNA_SUBMIT_NO_IMPLICIT;
                req.fence_fd = in_fence_fd;
```

Rebuild and `export ETNA_DUMP=1` prior to running `wpe-testbed-wayland`. This will generate
`/tmp/cmd_*.buf` files -- one file per command stream submission.

It's very useful to launch `wpe-testbed-wayland` with one set of parameters, dump the command
stream and repeat, while toggling e.g. just a single parameter (e.g. with `--rbo` or without)
to see the effect on the generated command stream -- very helpful and enlightening.

## Analyze command stream

1. Clone `etna_viv` repository

```
git clone https://github.com/etnaviv/etna_viv.git
```

2. Create container with legacy Python 2.7 environment to run the `etna_viv` debugging tools

```
cd /path/to/etna_viv
cat >>Containerfile <<EOF
FROM debian:buster-slim
RUN apt-get -y update && apt-get install -y python-minimal python-lxml
EOF
podman build --tag etna-debug .
```

3. Run container and use it to dump command streams

```
podman run -it  --mount=type=bind,source=/home,destination=/host/home etna-debug
/host/home/path-to-etna_viv-checkout/dump_separate_cmdbuf.py -l -b your_cmd_stream.buf
```

Example output:

```
    0x00000000, /*   GL.API_MODE := OPENGL */
    0x34000001, /*   PA.W_CLIP_LIMIT := 0x34000001 */
    0x00000000, /*   PA.FLAGS := UNK24=0,ZCONVERT_BYPASS=0 */
    0x38a01404, /*   PA.VIEWPORT_UNK00A80 := 0.000076 */
    0x46000000, /*   PA.VIEWPORT_UNK00A84 := 8192.000000 */
    0x00000000, /*   PA.ZFARCLIPPING := 0x0 */
    0x00007000, /*   RA.HDEPTH_CONTROL := UNK0=0,COMPARE=0x7 */
    0x00000000, /*   PS.CONTROL_EXT := OUTPUT_MODE0=NORMAL,OUTPUT_MODE1=NORMAL,OUTPUT_MODE2=NORMAL,OUTPUT_MODE3=NORMAL,OUTPUT_MODE4=NORMAL,OUTPUT_MODE5=NORMAL,OUTPUT_MODE6=NORMAL,OUTPUT_MODE7=NORMAL */
    0x00000808, /*   VS.HALTI1_UNK00884 := 0x808 */
    0x00000000, /*   RA.UNK00E0C := 0x0 */
    0x76543210, /*   PS.HALTI3_UNK0103C := 0x76543210 */
    0x6706667f, /*   PS.MSAA_CONFIG := 0x6706667f */
    0x00000000, /*   PE.HALTI4_UNK014C0 := 0x0 */
    0x00000001, /*   NTE.DESCRIPTOR_CONTROL := ENABLE=1 */
    0x00000002, /*   FE.HALTI5_UNK007D8 := 0x2 */
...
    0x00000000, /*   PE.ALPHA_COLOR_EXT1 := R=0.000000,A=0.000000 */
    0x00000000, /*   PE.STENCIL_CONFIG_EXT2 := MASK_BACK=0x0,WRITE_MASK_BACK=0x0 */
    0x00000000, /*   PE.MEM_CONFIG := COLOR_TS_MODE=128B,DEPTH_TS_MODE=128B */
    0xdeadbeef, /* PAD */
    0x00000000, /*   TS.MEM_CONFIG := DEPTH_FAST_CLEAR=0,COLOR_FAST_CLEAR=0,DEPTH_16BPP=0,DEPTH_AUTO_DISABLE=0,COLOR_AUTO_DISABLE=0,DEPTH_COMPRESSION=0,COLOR_COMPRESSION=0,COLOR_COMPRESSION_FORMAT=A4R4G4B4,UNK12=0,HDEPTH_AUTO_DISABLE=0,STENCIL_ENABLE=0,UNK21=0 */
    0xfe802000, /*   TS.COLOR_STATUS_BASE := *0xfe802000 */
    0xff012000, /*   TS.COLOR_SURFACE_BASE := *0xff012000 */
...
```

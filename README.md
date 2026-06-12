## Prerequisites
- NVIDIA drivers and NVML library
- `libmicrohttpd-dev`

## Build
```bash
gcc -o gpu_exporter gpu_nvlm_exporter.c -lnvidia-ml -lmicrohttpd -lpthread
```

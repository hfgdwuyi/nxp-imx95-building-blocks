# Building Blocks System Architecture — i.MX95 EVK

## NXP i.MX95 EVK — Complete System Design

---

## 1. SoC Overview

The NXP i.MX95 applications processor features:

| Component | Detail |
|-----------|--------|
| **CPU** | 6x Cortex-A55 @ 1.8GHz |
| **Real-time** | 1x Cortex-M7 @ 800MHz |
| **Safety** | 1x Cortex-M33 @ 333MHz |
| **GPU** | Imatec GPU (Vulkan/OpenGL ES) |
| **NPU** | eIQ Neutron 2+ TOPS |
| **ISP** | 2x Image Signal Processor |
| **Memory** | LPDDR5/LPDDR4X, up to 16GB |
| **Storage** | eMMC 5.1, SD 3.0 |

---

## 2. Architecture Layers

```
┌─────────────────────────────────────────────────────────┐
│                    Application Layer                     │
│  bb-cli (C)  │  REST API  │  MQTT Bridge  │  ...        │
├─────────────────────────────────────────────────────────┤
│                    blocks/ (product components)          │
│  bb-led (LED control)  │  bb-audio (intercom)  │  ...   │
├─────────────────────────────────────────────────────────┤
│                 middleware/ (streaming middleware)        │
│  bb_audio_stream (capture → codec → playback pipeline)  │
├─────────────────────────────────────────────────────────┤
│                   libbb (core library + framework)       │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐  │
│  │ Bus IPC  │ │  Config  │ │  Block   │ │  Thread  │  │
│  │ (AF_UNIX)│ │  (JSON)  │ │ Lifecycle│ │  Pool    │  │
│  │          │ │  Persist │ │ Recovery │ │  Log     │  │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘  │
├─────────────────────────────────────────────────────────┤
│                    hal/ (hardware abstraction layer)     │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐        │
│  │ LED  │ │ GPIO │ │ I2C  │ │ SPI  │ │ PWM  │        │
│  │ RTC  │ │ WDG  │ │ UART │ │Audio │ │Display│        │
│  └──────┘ └──────┘ └──────┘ └──────┘ └──────┘        │
├─────────────────────────────────────────────────────────┤
│              Linux Kernel (sysfs / dev / ioctl)           │
└─────────────────────────────────────────────────────────┘
```

## 3. Partition Scheme (A/B Boot)

```
Device         Size     Type      Label              Purpose
mmcblk0boot0   4 MiB    raw       —                  Boot ROM HW partition 0
mmcblk0boot1   4 MiB    raw       —                  Boot ROM HW partition 1
mmcblk0p1      8 MiB    raw       uboot              SPL + U-Boot + ATF + SCU FW
mmcblk0p2      8 KiB    raw       uboot-env-a        U-Boot environment A
mmcblk0p3      8 KiB    raw       uboot-env-b        U-Boot environment B
mmcblk0p4      64 MiB   vfat      boot-a             Kernel + DTB (slot A)
mmcblk0p5      64 MiB   vfat      boot-b             Kernel + DTB (slot B)
mmcblk0p6      1536 MiB ext4      rootfs-a           Rootfs (slot A)
mmcblk0p7      1536 MiB ext4      rootfs-b           Rootfs (slot B)
mmcblk0p8      512 MiB  ext4      recovery           Recovery rootfs (minimal)
mmcblk0p9      256 MiB  ext4      persist            Persistent data
mmcblk0p10     1 MiB    raw       manufacturing      Manufacturing data
mmcblk0p11     512 MiB  ext4      log                Dedicated log partition
```

## 4. Key Design Decisions

- **Zero runtime dependencies** — statically linked binaries, no Python/Node required
- **Three-layer separation** — HAL encapsulates hardware, middleware manages streaming pipelines, libbb provides common capabilities
- **HAL only does hardware abstraction** — no threads, no buffer management in HAL
- **Unix domain socket bus** — lightweight IPC, text-based protocol (PUB/SUB/UNSUB/PING)
- **A/B boot scheme** — dual-slot with boot_attempt counter for automatic fallback
- **~300KB per service** — static linking, no runtime overhead

## 5. i.MX95 vs i.MX8MP Key Differences

| Feature | i.MX8MP | i.MX95 |
|---------|---------|--------|
| **UART device** | `/dev/ttymxcX` | `/dev/ttyLPX` (LPUART) |
| **CPU cores** | 4x A53 | 6x A55 |
| **Audio** | NAU8822 (card 2) | WM8960/WM8962 (card 0) |
| **Display** | DSI/LVDS/HDMI | DSI/LVDS/HDMI/DP |
| **SPI** | ECSPI | LPSPI |
| **Kernel** | 5.4.70 | 6.6 LTS |

## 6. Build

```bash
# Native on the board
make

# Cross-compile for i.MX95 EVK (aarch64)
make cross

# Deploy to board
make deploy TARGET_HOST=192.168.0.232

# Create update package
make bbu
```

## 7. Message Protocol

```
PUB /dev/bb-led/cmd {"cmd":"blink","on_ms":200,"off_ms":200}
SUB /dev/bb-led/#
PING → PONG
```

## 8. Audio Pipeline

```
capture_thread (CPU0, SCHED_FIFO) → SPSC pool → codec_thread (CPU1) → MPSC pool → playback_thread (CPU2)
    bb_hal_audio read             4 frames     passthrough/opus      4 frames     bb_hal_audio write
```

- **PCM passthrough** (default): capture → memcpy → playback, <50ms latency
- **Pluggable codec**: `bb_audio_codec_ops_t` — future Opus encode/decode
- **xrun recovery**: auto-recover on EPIPE, error on 10 consecutive xruns

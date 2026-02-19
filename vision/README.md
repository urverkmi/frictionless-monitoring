# AprilTag Multicore Tracker (OpenCV + GStreamer + libcamera)

High-performance AprilTag tracking pipeline using:

* libcamera (via GStreamer)
* OpenCV
* AprilTag C library
* Multithreading pipeline:

  * Capture Thread
  * Low-Resolution Detection Thread
  * High-Resolution Pose Estimation Thread
  * Visualization Thread

Optimized for high-resolution sensors such as Sony IMX477 (4056×3040).

---

# Features

* Fast low-resolution detection for ROI extraction
* High-precision pose estimation on full resolution ROI
* Multicore pipeline for maximum performance
* Uses real camera timestamps
* Supports manual exposure and gain control
* Automatic display scaling

---

# Pipeline Architecture

```
Camera (libcamera)
    │
    ▼
Capture Thread
    │
    ▼
Low-Res Detection Thread
    │
    ▼
High-Res Pose Thread
    │
    ▼
Visualization Thread
```

---

# Requirements

Tested on:

* Linux (Raspberry Pi OS / Ubuntu recommended)
* OpenCV ≥ 4.5
* GStreamer ≥ 1.18
* libcamera ≥ 0.0.5
* AprilTag library

---

# Install Dependencies

## Ubuntu / Debian / Raspberry Pi OS

```bash
sudo apt update

sudo apt install -y \
build-essential \
cmake \
git \
pkg-config \
libopencv-dev \
libgstreamer1.0-dev \
libgstreamer-plugins-base1.0-dev \
gstreamer1.0-tools \
gstreamer1.0-libcamera \
libcamera-dev
```

---

# Install AprilTag Library

Clone and build:

```bash
git clone https://github.com/AprilRobotics/apriltag.git
cd apriltag

cmake .
make -j4

sudo make install
sudo ldconfig
```

---

# Verify libcamera GStreamer plugin

Run:

```bash
gst-inspect-1.0 libcamerasrc
```

If installed correctly, it should display plugin information.

---

# Build the Project

## Single File Compile

Save code as:

```
apriltag_multicore.cpp
```

Compile:

```bash
g++ apriltag_multicore.cpp -o apriltag_multicore \
-O3 \
-std=c++17 \
$(pkg-config --cflags --libs opencv4 gstreamer-1.0 gstreamer-app-1.0) \
-lapriltag \
-lpthread
```

---

# Run

```bash
./apriltag_multicore
```

Press:

```
q
```

to quit.

---

# Camera Configuration

Inside the code:

```
constexpr int resoluion_divider = 2;
```

Options:

| value | resolution |
| ----- | ---------- |
| 1     | 4056×3040  |
| 2     | 2028×1520  |
| 4     | 1014×760   |

---

Manual exposure settings:

```
exposure-time=1500
analogue-gain=15
```

Adjust based on lighting.

---

# Camera Calibration

Update camera matrix:

```
cv::Mat K
cv::Mat D
```

Values must match your camera resolution.

If using resolution divider, parameters must also be divided.

---

# Performance Tips

For maximum FPS:

```
resoluion_divider = 2
quad_decimate lowres = 1.0
quad_decimate highres = 2–4
```

For maximum precision:

```
resoluion_divider = 1
quad_decimate highres = 1.0
```

---

# Thread Overview

| Thread  | Purpose                  |
| ------- | ------------------------ |
| Capture | Reads camera frames      |
| LowRes  | Fast detection           |
| HighRes | Accurate pose estimation |
| Vis     | Display                  |

---

# Troubleshooting

## libcamerasrc not found

Install:

```bash
sudo apt install gstreamer1.0-libcamera
```

---

## apriltag not found

Run:

```bash
sudo ldconfig
```

---

## OpenCV not found

Install:

```bash
sudo apt install libopencv-dev
```

---

# Example Output

```
X=0.12 Y=0.03 Yaw=14.2
```

---


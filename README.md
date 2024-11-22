### Gstreamer

```bash
# gstreamer
sudo apt install libgstrtspserver-1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer1.0-dev librga-dev

# rknn
wget https://raw.githubusercontent.com/airockchip/rknn-toolkit2/refs/heads/master/rknpu2/runtime/Linux/librknn_api/aarch64/librknnrt.so
```

```bash
# gst-test
export GST_DEBUG=2
export GST_VIDEO_CONVERT_USE_RGA=1
export GST_VIDEO_FLIP_USE_RGA=1
export GST_DEBUG=3
export DISPLAY=:0.0
```

<img src="demo.jpg" title="DEMO" width="50%">

```bash
# file
gst-launch-1.0 filesrc location=/rockchip-test/bbb_sunflower_1080p_h264_30fps_aac.mp4 ! parsebin ! mppvideodec ! waylandsink render-rectangle='<0,0,400,400>' &
gst-launch-1.0 filesrc location=/rockchip-test/bbb_sunflower_1080p_h264_30fps_aac.mp4 ! parsebin ! mppvideodec ! waylandsink render-rectangle='<400,0,400,400>' &
gst-launch-1.0 filesrc location=/rockchip-test/bbb_sunflower_1080p_h264_30fps_aac.mp4 ! parsebin ! mppvideodec ! waylandsink render-rectangle='<800,0,400,400>' &
gst-launch-1.0 filesrc location=/rockchip-test/bbb_sunflower_1080p_h264_30fps_aac.mp4 ! parsebin ! mppvideodec ! waylandsink render-rectangle='<1200,0,400,400>' &
gst-launch-1.0 filesrc location=/rockchip-test/bbb_sunflower_1080p_h264_30fps_aac.mp4 ! parsebin ! mppvideodec ! waylandsink render-rectangle='<0,400,400,400>' &
gst-launch-1.0 filesrc location=/rockchip-test/bbb_sunflower_1080p_h264_30fps_aac.mp4 ! parsebin ! mppvideodec ! waylandsink render-rectangle='<400,400,400,400>' &
gst-launch-1.0 filesrc location=/rockchip-test/bbb_sunflower_1080p_h264_30fps_aac.mp4 ! parsebin ! mppvideodec ! waylandsink render-rectangle='<800,400,400,400>' &
gst-launch-1.0 filesrc location=/rockchip-test/bbb_sunflower_1080p_h264_30fps_aac.mp4 ! parsebin ! mppvideodec ! waylandsink render-rectangle='<1200,400,400,400>' &

# rtsp
gst-launch-1.0 rtspsrc location=rtsp://admin:admin123@192.168.44.108:554/live/main protocols=tcp ! rtph264depay ! h264parse ! mppvideodec ! videoscale ! video/x-raw,width=1280,height=720 ! videoconvert ! video/x-raw,format=RGBA ! waylandsink render-rectangle='<1200,0,400,400>'

# rtsp server
./test-launch "( v4l2src min-buffers=64 ! video/x-raw,format=NV12,framerate=30/1 ! mpph264enc rc-mode=vbr bps-max=4000000 ! rtph264pay name=pay0 pt=96 config-interval=-1 )"
```
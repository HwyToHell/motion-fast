# test/mem-usage

### start ffserver
ffserver -d -f ~/app-dev/motion-fast/test/ffserver-rtsp.conf

### feed ffmpeg input 1920x1080
ffmpeg -re -i 2021-10-05_14h05m01s.mp4 -vcodec h264 -preset ultrafast http://localhost:8090/feed1.ffm -hide_banner

### run motion detector
in ~/app-dev/build/libav-Debug
./motion rtsp://localhost:5454/test1-rtsp.mpg

### optional: show video stream for visual diagnostics
ffplay rtsp://localhost:5454/test1-rtsp.mpg

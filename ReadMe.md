# Realtime Motion Analyzer

## Detect motion on webcam video stream and save video sequences to disk

### Branches
- main
   - motion analyzer service for linux and raspberry pi
   - video source: HD webcam (HQCAM - rtsp stream)
   - motion footage will be saved to SD card as they are detected
- diag-pic
   - at each new motion detection pre-buffered motion pictures  aredetermine settings for real time analysis on raspberry pi
   - video source: HD webcam (HQCAM - rtsp stream)

### Usage
- run: motion rtsp://admin:@192.168.1.10
- enable diagPics in main: appState.debug = true;

### Project Structure
- src, header flat in project directory
    - packet-buffer.cpp
      first prototype, derived from libav remuxing.c example
- __test__
    - [show-diag-pics.cpp](show-diag-pics.cpp)
      test motion detection diagnostics
      read frames from /dev/video0
      create pre-triggered diag pics when motion was detected
    - [avreadwrite-test.cpp](avreadwrite-test.cpp)
      simple test app using avreadwrite classes
      used for memory leak detection with valgrind

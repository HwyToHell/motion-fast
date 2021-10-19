# Realtime Motion Analyzer

## Detect motion on webcam video stream and save video sequences to disk

### Branches
- dev
   - development branch (merge with main, if stable)
- main
   - motion analyzer service for linux and raspberry pi
   - video source: HD webcam (HQCAM - rtsp stream)
   - motion footage will be saved to SD card as they are detected
- test/mem-usage
   - test memory usage on raspberry pi in order to estimate long term stability
   - in debug mode: generate at each new motion detection pre-buffered motion pictures
   - used to determine settings for real time analysis on raspberry pi
   - video source: RTST stream (HQCAM or ffserver)

### Usage
- run: motion rtsp://admin:@192.168.1.10
- enable diagPics in main: appState.debug = true;

### Project Structure
- src, header flat in project directory
    - packet-buffer.cpp
      first prototype, derived from libav remuxing.c example
    - libavreadwrite-test.cpp
      test packet buffer (alloc, ref-count) by reading mp4 video file
- __test__
    - [show-diag-pics.cpp](show-diag-pics.cpp)
      test motion detection diagnostics
      read frames from /dev/video0
      create pre-triggered diag pics when motion was detected
    - [avreadwrite-test.cpp](avreadwrite-test.cpp)
      simple test app using avreadwrite classes
      used for memory leak detection with valgrind

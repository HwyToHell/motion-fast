# Realtime Motion Analyzer

## Detect motion on webcam video stream and save video sequences to disk

### Branches
- dev
   - development branch (merge with main, if stable)
- main
   - motion analyzer service for linux and raspberry pi
   - video source: HD webcam (HQCAM - rtsp stream)
   - motion footage will be saved to SD card as they are detected
- diag-pic
   - at each new motion detection pre-buffered motion pictures are saved to disk
   - determine settings for real time analysis on raspberry pi
   - video source: HD webcam (HQCAM - rtsp stream)
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
- __cam-config__
    - Python scripts for IP cam configuration
    - based on NeiroNx/python-dvr
- __doc__
    - OpenCV VideoReader implementation
- __firebase__
    - python scripts for uploading video files to firebase
    - uses pyrebase package
    - credentials in separate dir for github security
- __performance__
    - performance measurement results
- __poc__ proof of concept
    - libav C examples (decode, remux)
    - [decode-buffer.cpp](poc/decode-buffer.cpp)
      read and decode in one thread, using the libav C-functions
    - [rtsp-mux.cpp](poc/rtsp-mux.cpp)
      read buffered input of rtsp stream and write to disk in second thread
- __sh__
    - shellscripts for Raspberry Pi
- __test__
    - [avreadwrite-test.cpp](test/avreadwrite-test.cpp)
      simple test app using avreadwrite classes by reading video file
      used for memory leak detection with valgrind
    - [show-diag-pics.cpp](test/show-diag-pics.cpp)
      test motion detection diagnostics
      read frames from /dev/video0
      create pre-triggered diag pics when motion was detected
    - [sigusr-test.cpp](test/sigusr-test.cpp)
      test using SIGUSR1 to end process gracefully



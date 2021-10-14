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

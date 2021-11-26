#!/bin/bash

echo $(date +'%Y-%m-%d %H:%M:%S') "Restart motion and firebase uploader"

# if there is a motion process, terminate it by sending SIGUSR1
motion_pid=$(pgrep -x motion)
if [ ! -z $motion_pid ]
then
    echo $(date +'%Y-%m-%d %H:%M:%S') "SIGUSR1 to 'motion' process (PID $motion_pid) sent, wait for finish"
    kill -s SIGUSR1 $motion_pid
    tail --pid=$motion_pid -f /dev/null
    echo $(date +'%Y-%m-%d %H:%M:%S') "Instance of 'motion' has finished, restart"
else
    echo $(date +'%Y-%m-%d %H:%M:%S') "No 'motion' instance running, start new one"
fi

# save and timestamp log
/home/pi/bin/timestamp-log.sh /home/pi/motion.log

# set camera time
python3 /home/pi/bin/set-cam-time.py

# start motion as background process
/home/pi/motion rtsp://admin:@192.168.1.10 &

# delete video files older than 14 days to avoid SD memory shortage
/home/pi/bin/rm-old-videos.sh 14 /home/pi

# if there is an upload-videos process, terminate it by sending SIGUSR1
upload_pid=$(pgrep upload-videos)
if [ ! -z $upload_pid ]
then
    echo $(date +'%Y-%m-%d %H:%M:%S') "SIGUSR1 to 'upload-videos' process (PID $upload_pid) sent, wait for finish"
    kill -s SIGUSR1 $upload_pid
    tail --pid=$upload_pid -f /dev/null
	echo $(date +'%Y-%m-%d %H:%M:%S') "Instance of 'upload-videos' has finished, restart"
else
	echo $(date +'%Y-%m-%d %H:%M:%S') "No 'upload-videos' instance running, start new one"
fi

# start upload-videos as background process
/home/pi/bin/upload-videos.py &

# delete video files older than 5 days from server
/home/pi/bin/remove-old-remote-videos.py


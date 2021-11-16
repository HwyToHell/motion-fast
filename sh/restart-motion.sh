#!/bin/bash

echo "restart motion process"

# if there is a motion process, terminate it by sending SIGUSR1
motion_pid=$(pgrep -x motion)
if [ ! -z $motion_pid ]
then
        echo "send SIGUSR1 to $motion_pid"
        kill -s SIGUSR1 $motion_pid
        echo "wait 5 sec for process to finish"
        sleep 5
fi

# save and timestamp log
/home/pi/bin/timestamp-log.sh /home/pi/motion.log

# set camera time
python3 /home/pi/bin/set-cam-time.py

# start motion as background process
/home/pi/motion rtsp://admin:@192.168.1.10 &

# delete video files older than 7 days to avoid SD memory shortage
/home/pi/bin/rm-old-videos.sh 7 /home/pi

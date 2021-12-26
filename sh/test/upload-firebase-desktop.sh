#!/bin/bash

echo $(date +'%Y-%m-%d %H:%M:%S') "Restart firebase uploader"

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
/home/holger/app-dev/motion-fast/sh/upload-videos-desktop.py &

# delete video files older than 5 days from server
/home/holger/app-dev/motion-fast/sh/remove-old-remote-videos-desktop.py
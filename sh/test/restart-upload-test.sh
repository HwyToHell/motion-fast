#!/bin/bash
path_bin=/home/holger/app-dev/motion-fast/sh
path_credentials=/home/holger/app-dev/firebase-credentials
path_monitor=/home/holger/temp

# if there is an upload-videos process, terminate it by sending SIGUSR1
upload_pid=$(pgrep upload-videos)
if [ ! -z $upload_pid ]
then
    echo $(date +'%Y-%m-%d %H:%M:%S') "SIGUSR1 to 'upload-videos' process (PID $upload_pid) sent, wait for finish"
    kill -s SIGUSR1 $upload_pid
    tail --pid=$upload_pid -f /dev/null
    sleep 3
	echo $(date +'%Y-%m-%d %H:%M:%S') "Instance of 'upload-videos' has finished, restart"
else
	echo $(date +'%Y-%m-%d %H:%M:%S') "No 'upload-videos' instance running, start new one"
fi

# start upload-videos as background process
$path_bin/upload-videos.py $path_monitor $path_credentials &

# delete video files older than 5 days from server
$path_bin/remove-old-remote-videos.py $path_credentials
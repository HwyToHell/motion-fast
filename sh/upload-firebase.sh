#!/bin/bash

echo $(date +'%Y-%m-%d %H:%M:%S') "restart firebase uploader"

# if there is an upload-videos process, terminate it by sending SIGUSR1
upload_pid=$(pgrep upload-videos)
if [ ! -z $upload_pid ]
then
    echo $(date +'%Y-%m-%d %H:%M:%S') "SIGUSR1 to upload-videos process (PID $upload_pid) sent, wait for finish"
    kill -s SIGUSR1 $upload_pid
    tail --pid=$upload_pid -f /dev/null
	echo $(date +'%Y-%m-%d %H:%M:%S') "upload-videos has finished, restart"
else
	echo $(date +'%Y-%m-%d %H:%M:%S') "no upload-videos process running, restart"
fi

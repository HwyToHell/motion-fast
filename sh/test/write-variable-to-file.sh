#!/bin/bash

pid=12345
echo "pid written: "$pid
echo "pid=""$pid" > motion-pid
source motion-pid
echo "pid read: "$pid

# pgrep motion
# -or-
# ps -p <motion-pid>

# send SIGUSR1 to terminate process gracefully
# check exit code

# start motion as backgorund process and get pid
# pid=./motion & $!
# probably in a separate shell

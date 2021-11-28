#!/bin/bash
# save log file (given as $1) as yyyy-mm-dd_HHhMMmSSs.log with current time stamp

# validate command line arguments
if [ $# -lt 1 ]
then
	echo "Usage timestamp-log.sh <logfile>"
	exit 1
fi
logfilePath=$1
if [ ! -f $logfileAbsPath ]
then
	echo "Logfile $logfilePath does not exist"
	exit 2
fi 

# timestamped filename
logfile=${logfilePath##*/}
logStamped=$(date +%Y-%m-%d_%Hh%Mm%Ss).log

# extract path
if [[ $logfilePath == */* ]]
then
	path=${logfilePath%/*}
	logStampedPath=$path/$logStamped
else
	logStampedPath=$logStamped
fi

cp $logfilePath $logStampedPath
> $logfilePath


echo $(date +'%Y-%m-%d %H:%M:%S') "Log saved to: $logStampedPath"


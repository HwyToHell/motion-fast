#!/usr/bin/python3
# with this shebang the script name shows up in process list,
# if run from command line as ./upload-videos.py
# 
# usage: remove-old-remote-videos.py <path-to-credentials>
 
import os, sys
import pyrebase
from datetime import datetime, timedelta


if len(sys.argv) < 2:
    print(f"usage: {sys.argv[0]} <path-to-credentials>")
    sys.exit(-1)

path_to_credentials = sys.argv[1]
if not os.path.isdir(path_to_credentials):
    print(f"path-to-credentials: '{path_to_credentials}' is not a directory")   
    sys.exit(-2)


def time_stamp():
    return datetime.now().strftime('%Y-%m-%d %H:%M:%S')


def date_today_midnight():
    today = datetime.now()
    return datetime(today.year, today.month, today.day)


def get_files_to_remove(file_list, days_in_history):
    files_to_delete = []
    days_history = timedelta(days=days_in_history)

    for filename in file_list:
        # process video files only
        if filename.endswith(".mp4"):
            filename_split = filename.split('_', maxsplit=1)
            # print(f"\nfilename {filename} split by '_' in {filename_split[0]}")

            try:
                date_from_file = datetime.strptime(filename_split[0], "%Y-%m-%d")
                date_diff = date_today_midnight() - date_from_file
                # print(date_diff)
                if (date_diff) > days_history:
                    # print(f"delete file from {date_diff} before")
                    files_to_delete.append(filename)
            except ValueError as e:
                print(f"{time_stamp()} Error parsing time from filename: {filename}\n {e}")

    return files_to_delete


### remove all video files older than discard_days_prior
discard_days_prior = 5
discard_date_prior = date_today_midnight() - timedelta(days=discard_days_prior)
print(f"{time_stamp()} Removing files prior to {discard_date_prior} from firebase storage")


### enable serves for firebase storage 
sys.path.append(path_to_credentials)
service_acct_file = path_to_credentials + "/serviceAccountKey.json"
from firebaseconfig import config
config["serviceAccount"] = service_acct_file
firebase = pyrebase.initialize_app(config)
try:
    storage = firebase.storage()
except BaseException as e:
    print(f"{time_stamp()} Firebase storage not reachable: {type(e).__name__}")
    print(f"{time_stamp()} No files deleted, exiting application")
    sys.exit(-1)

# get all filenames of firebase storage
remote_file_names = []
remote_files = storage.list_files()
for file in remote_files:
    remote_file_names.append(file.name)

# remove old files
backlog_remove = get_files_to_remove(remote_file_names, discard_days_prior)
for file in list(backlog_remove):
    try:
        storage.delete(file)
        backlog_remove.remove(file)
    except BaseException as e:
        print(type(e).__name__)

# log message on success
if len(backlog_remove) == 0:
    print(f"{time_stamp()} All files removed")
else:
    print(f"{time_stamp()} Error, was not able to remove these files:\n {backlog_remove}")
    

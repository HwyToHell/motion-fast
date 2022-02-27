#!/usr/bin/python3
# 
# upload files in current directory, specified by <pattern>
#   to firebase by using credentials in <path-to-credentials>
#
# usage: upload-fb-manual.py <date> <path-to-credentials>

import os, sys, time
from datetime import datetime
import pyrebase


### fcns ###
def time_stamp():
    return datetime.now().strftime('%Y-%m-%d %H:%M:%S')


def upload_closed_file(storage, full_path_name):
    # timeout for retry in seconds, doubled after each unsuccessful attempt
    timeout = 1 
    retry_cnt = 3

    file_name = os.path.basename(full_path_name)
    print(f"{time_stamp()} Trying to upload: {file_name}", flush=True)
    attempts_left = retry_cnt
    while attempts_left:
        attempts_left -=1
        # print("attempts left", attempts_left)

        # try uploading to firebase storage
        try:
            ret = storage.child(file_name).put(full_path_name)
            #print(ret)
            print(f"{time_stamp()} Upload successful: {file_name}", flush=True)
            attempts_left = 0
            timeout = 1
            return True
        # ConnectionError
        #except requests.exceptions.RequestException as err:
        except BaseException as err:  
            print(f"{time_stamp()} Upload error: {type(err).__name__}", flush=True)
            print(f" trying to reconnect in {timeout} sec", flush=True)
            time.sleep(timeout)
            timeout = timeout * 2 if timeout < 60 else 60

    print(f"{time_stamp()} Was not able to connect", flush=True)
    return False


def print_upload_list():
    print("--- print upload list ---", flush=True)
    for file in upload_list:
        print(file, flush=True)


### main ###

# validate args
if len(sys.argv) < 3:
    print(f"usage: {sys.argv[0]} <date> <path-to-credentials>")
    sys.exit(-1) 

path_to_credentials = sys.argv[2]
if not os.path.isdir(path_to_credentials):
    print(f"path-to-credentials: '{path_to_credentials}' is not a directory")   
    sys.exit(-2)

date_string = sys.argv[1]
try:
    date = datetime.strptime(date_string, "%Y-%m-%d")
except ValueError as e:
    print(f"date: {e}")
    sys.exit(-3)


# generate upload list
files_to_upload = []
entries = os.listdir() 
for entry in entries:
    if os.path.isfile(entry):
        if entry.startswith(date_string) and entry.endswith(".mp4") :
            files_to_upload.append(entry)

print(f"{time_stamp()} Files for upload in {os.getcwd()}:")
for file in files_to_upload:
    print(f" {file}")


# enable service for firebase storage
sys.path.append(path_to_credentials)
service_acct_file = path_to_credentials + "/serviceAccountKey.json"
from firebaseconfig import config
config["serviceAccount"] = service_acct_file
firebase = pyrebase.initialize_app(config)

# upload loop
try:
    storage = firebase.storage() 
    timeout = 1
    
    for file in list(files_to_upload):
        # print_upload_list()
        if upload_closed_file(storage, file):
            # print("delete from upload_list:", file_name)
            files_to_upload.remove(file)
        else:
            print(f"{time_stamp()} Upload not successful", flush=True)
except BaseException as e:
    print(f"{time_stamp()} Firebase storage not reachable: {type(e).__name__}", flush=True)

print(f"{time_stamp()} Files left for upload: {len(files_to_upload)}", flush=True)
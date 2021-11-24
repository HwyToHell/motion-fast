#!/usr/bin/python3
# with this shebang the script name shows up in process list,
# if run from command line as ./upload-videos.py

import os, signal, sys, time
from threading import Event
import pyrebase
import requests  #ConnectionError exceptions
from datetime import datetime
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler


path = "/home/holger/temp"
upload_list = []


def time_stamp():
    return datetime.now().strftime('%Y-%m-%d %H:%M:%S')


#### handler for terminating app with SIGUSR1
terminate = Event()
def signalHandler(sig, frame):
    print(f"{time_stamp()} SIGUSR1 received, terminate upload-videos.py")
    terminate.set()
signal.signal(signal.SIGUSR1, signalHandler)
print(f"{time_stamp()} Send SIGUSR1 to PID {os.getpid()} to terminate")


#### watchdog for new video files in {path}
class OnAny(FileSystemEventHandler):
    def on_any_event(self, event):
        print(f'event type: {event.event_type} path: {event.src_path}')

class OnCreated(FileSystemEventHandler):
    def on_created(self, event):
        # print(f'file created: {event.src_path}')
        if ".mp4" in event.src_path:
            fileCreated = {'name': event.src_path, 'is_closed': False}
            upload_list.append(fileCreated)
            print(f"{time_stamp()} Video file {event.src_path} queued for uploading")

class OnClosed(FileSystemEventHandler):
    def on_closed(self, event):
        # print(f'file closed: {event.src_path}')
        if ".mp4" in event.src_path:
            set_closed(event.src_path)  

def set_closed(fileName):
    for file in upload_list:
        if file['name'] == fileName:
            file['is_closed'] = True
            return True
    return False

# setup and run watchdog
on_closed_handler = OnClosed()
on_created_handler = OnCreated()
observer = Observer()
observer.schedule(on_closed_handler, path, recursive=False)
observer.schedule(on_created_handler, path, recursive=False)
print(f"{time_stamp()} Monitoring directory {path} for new video files")
observer.start()


### fcns for uploading to firebase
def upload_closed_file(storage, full_path_name):
    # timeout for retry in seconds, doubled after each unsuccessful attempt
    timeout = 1 
    retry_cnt = 3

    file_name = os.path.basename(full_path_name)
    print(f"{time_stamp()} Trying to upload: {file_name}")
    attempts_left = retry_cnt
    while attempts_left:
        attempts_left -=1
        # print("attempts left", attempts_left)

        # try uploading to firebase storage
        try:
            ret = storage.child(file_name).put(full_path_name)
            #print(ret)
            print(f"{time_stamp()} Upload successful: {file_name}")
            attempts_left = 0
            return True
        # ConnectionError
        except requests.exceptions.RequestException as err:  
            print(type(err).__name__)
            print(f"{time_stamp()} Waiting for {timeout} sec to reconnect")
            time.sleep(timeout)
            timeout *= 2

    print(f"{time_stamp()} Was not able to connect")
    return False


def process_upload_list(store):
    for file in list(upload_list):
        # print_upload_list()
        if terminate.is_set():
            break
        else:
            file_name = file['name']
            if upload_closed_file(store, file_name):
                # print("delete from upload_list:", file_name)
                upload_list.remove(file)
            else:
                print(f"{time_stamp()} Upload not successful")


def print_upload_list():
    print("--- print upload list ---")
    for file in upload_list:
        print(file)


# enable service for firebase storage
sys.path.append("/home/holger/app-dev/firebase-credentials")
from firebaseconfig import config
firebase = pyrebase.initialize_app(config)

# trying to reconnect with max timeout of 60 sec
timeout = 1
while not terminate.is_set():
    try:
        storage = firebase.storage() 

        # upload loop - repeat every 60 sec until terminated by SIGUSR1
        while not terminate.is_set():
            process_upload_list(storage)
            terminate.wait(10)

    except BaseException as e:
        timeout = timeout * 2 if timeout < 60 else 60
        print(f"{time_stamp()} Firebase storage not reachable: {type(e).__name__}")
        print(f" trying to reconnect in {timeout} sec")
    terminate.wait(timeout)



observer.stop()
observer.join()
print(f"{time_stamp()} Files left for upload: {len(upload_list)}")
for item in upload_list:
    print(f" {item['name']}")
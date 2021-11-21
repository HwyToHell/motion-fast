#!/usr/bin/python3
# with this shebang the script name shows up in process list,
# if run from command line as ./upload-videos.py

import os, signal, sys, time
from threading import Event
import pyrebase
import requests  #ConnectionError exceptions
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler


path = "/home/holger/temp"
upload_list = []

### TODO set process name
# https://stackoverflow.com/questions/2255444/changing-the-process-name-of-a-python-script/18992161


#### handler for terminating app with SIGUSR1
terminate = Event()
def signalHandler(sig, frame):
    print("SIGUSR1 received, terminate upload-videos.py")
    terminate.set()
signal.signal(signal.SIGUSR1, signalHandler)
print(f"send SIGUSR1 to PID {os.getpid()} to terminate")


#### watchdog for new video files in {path}
class OnAny(FileSystemEventHandler):
    def on_any_event(self, event):
        print(f'event type: {event.event_type} path: {event.src_path}')

class OnCreated(FileSystemEventHandler):
    def on_created(self, event):
        print(f'file created: {event.src_path}')
        if ".mp4" in event.src_path:
            fileCreated = {'name': event.src_path, 'is_closed': False}
            upload_list.append(fileCreated)
            print(f"video file {event.src_path} put into upload_list")

class OnClosed(FileSystemEventHandler):
    def on_closed(self, event):
        print(f'file closed: {event.src_path}')
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
observer.start()
print(f"monitoring directory {path} for new video files")


### service for uploading to firebase
sys.path.append("/home/holger/app-dev/firebase-credentials")
from firebaseconfig import config
firebase = pyrebase.initialize_app(config)
store = firebase.storage()

def upload_closed_file(storage, full_path_name):
    # timeout for retry in seconds, doubled after each unsuccessful attempt
    timeout = 1 
    retry_cnt = 3

    file_name = os.path.basename(full_path_name)
    print("trying to upload", file_name)
    attempts_left = retry_cnt
    while attempts_left:
        attempts_left -=1
        print("attempts left", attempts_left)

        # try uploading to firebase storage
        try:
            ret = storage.child(file_name).put(full_path_name)
            #print(ret)
            print("upload successful:", file_name)
            attempts_left = 0
            return True
        # ConnectionError
        except requests.exceptions.RequestException as err:  
            print(type(err).__name__)
            print("waiting for", timeout, "sec to reconnect")
            time.sleep(timeout)
            timeout *= 2

    print("was not able to connect")
    return False

def process_upload_list():
    for file in list(upload_list):
        print_upload_list()
        if terminate.is_set():
            break
        else:
            file_name = file['name']
            if upload_closed_file(store, file_name):
                print("delete from upload_list:", file_name)
                upload_list.remove(file)
            else:
                print("upload not successful")

def print_upload_list():
    print("---upload list---")
    for file in upload_list:
        print(file)



# upload loop - repeat every 60 sec until terminated by SIGUSR1
while not terminate.is_set():
    process_upload_list()
    terminate.wait(10)


observer.stop()
observer.join()
print("files left to upload:", upload_list)
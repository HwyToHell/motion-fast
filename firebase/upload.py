import sys
import pyrebase
#import requests
import time

def uploadRetry(storage, file):
    retryCnt = 3
    timeout = 1

    print("trying to upload", file)
    attemptsLeft = retryCnt
    while attemptsLeft:
        attemptsLeft -=1
        print("attempts left", attemptsLeft)

        # upload videos
        try:
            # ret = storage.child("2021-11-05.mp4").put("2021-11-04_03h01m04s.mp4")
            ret = storage.child(file).put(file)
            print(ret)
            print("upload successful:", file)
            attemptsLeft = 0
            return True
        except requests.exceptions.RequestException as err:  
        # except BaseException as err:
            # e = sys.exc_info()[0]
            # print(f"unexpected {err=}, {type(err)=}")
            
            print(type(err).__name__)
            #print("Name2:", type(err.args[0]).__name__)
            #print("Name3:", type(err.args[0].args[0]).__name__)
            # print(err)
            # print("err.args[0]:", err.args[0])
            # print("err.args[0].args[0]:", err.args[0].args[0])

            print("waiting for", timeout, "sec to reconnect")
            time.sleep(timeout)
            timeout *= 2

    print("was not able to connect")
    return False

# storage service credentials
sys.path.append("/home/holger/app-dev/firebase-credentials")
from firebaseconfig import config
fbStorage = pyrebase.initialize_app(config)
store = fbStorage.storage()

files = ["2021-11-04_03h01m04s.mp4", "2021-05-10_14h09m00s.mp4"]

for file in files:
    if uploadRetry(store, file):
        print("upload successful, delete local file")
    else:
        print("upload not successful")


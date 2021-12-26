import sys
import pyrebase

### service for uploading to firebase
sys.path.append("/home/holger/app-dev/firebase-credentials")
from firebaseconfig import config
firebase = pyrebase.initialize_app(config)
storage = firebase.storage()

print(storage.storage_bucket)

# list all files in firebase storage
files = storage.list_files()
for file in files:
    print(file.name)
    #print(storage.child(file.name).get_url(None))

import datetime
tm1 = datetime.datetime.now()
print(tm1)
print(f"{tm1.year}-{tm1.month}-{tm1.day}")
tm2 = datetime.datetime(tm1.year, tm1.month, tm1.day)

diff = tm1 - tm2
print(diff.days, diff.seconds)


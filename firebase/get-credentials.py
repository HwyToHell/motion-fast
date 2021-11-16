import sys
sys.path.append("/home/holger/app-dev/firebase-credentials")

for p in sys.path:
    print(p)

from firebaseconfig import config

print("config:")
for c in config:
    print(c)

#!/usr/bin/env python3

from dvrip import DVRIPCam
from time import sleep

host_ip = '192.168.1.10'

cam = DVRIPCam(host_ip, user='admin', password='')
if cam.login():
	print("Success! Connected to " + host_ip)
else:
	print("Failure. Could not connect.")

print("Camera time:", cam.get_time())
cam.set_time()
print("Camera time:", cam.get_time())

cam.close()

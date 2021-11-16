#!/usr/bin/env python3

from dvrip import DVRIPCam

host_ip = '192.168.1.10'
cam = DVRIPCam(host_ip, user='admin', password='')

if cam.login():
        print("Device manager connected to " + host_ip)
else:
        print("Failure. Device manager was not able to connect.")

cam.set_time()
print("Camera time set to:", cam.get_time())

cam.close()
print("Device manager disconnected")

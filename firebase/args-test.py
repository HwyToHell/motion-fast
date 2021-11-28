#!/usr/bin/python3
# args: path_to_m

import os, sys

if len(sys.argv) < 3:
    print(f"usage: {sys.argv[0]} <path-to-monitor> <path-to-credentials>")
    sys.exit(-1) 

path_to_monitor = sys.argv[1]
path_to_credentials = sys.argv[2]
exit = False
if not os.path.isdir(path_to_monitor):
    print(f"path-to-monitor: '{path_to_monitor}' is not a directory")   
    exit = True
if not os.path.isdir(path_to_credentials):
    print(f"path-to-credentials: '{path_to_credentials}' is not a directory")   
    exit = True
if exit:
    sys.exit(-2)
    

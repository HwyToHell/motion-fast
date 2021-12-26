import os, sys

# extract filename from path
if len(sys.argv) < 2:
    print("usage: path-split.py <path>")
    sys.exit(-1)
path = sys.argv[1]
print("path:", path)
print("base name:", os.path.basename(path))

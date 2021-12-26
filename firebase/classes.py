from dataclasses import dataclass

@dataclass
class VideoFile:
    name: str
    isClosed: bool


file = VideoFile("2021-11-11.mp4", False)
file.isClosed = True
print(file.name)

from collections import deque
buffer = deque([file])
l = [file]
buffer.append(VideoFile("2021-11-12.mp4", False))

class VideoFile2:
    pass

file2 = VideoFile2()
#print(file2.name, file2.isClosed)

file2.name = "2021-11-10.mp4"
file2.isClosed = False

buffer2 = ([file2])


file3 = VideoFile2()
file3.name = "2022"
file3.isClosed = False

buffer2.append(file3)
print("buffer2:", buffer2)
idx = 0
for file in buffer2:
    print(idx, ":", file.name, file.isClosed)
    idx += 1

#a = buffer.pop()
#print(a.name, a.isClosed)

print(l[0].name, l[0].isClosed)
print("buffer:", buffer)

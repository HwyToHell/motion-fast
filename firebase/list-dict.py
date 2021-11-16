f1 = {'name': 'file1', 'isClosed': True}
f2 = {'name': 'file2', 'isClosed': True}
f3 = {'name': 'file3', 'isClosed': False}
files = [ f1, f2, f3 ]

def setClosed(uploadList, fileName):
    for file in uploadList:
        if file['name'] == fileName:
            file['isClosed'] = True
            return True
    return False



print(next((file for file in files if file['name'] == 'file3'), None))

print(setClosed(files, 'file3'))

# check if setClosed attribute has been set
print(next((file for file in files if file['name'] == 'file3'), None))

### with dataclass
from dataclasses import dataclass

@dataclass
class VideoFile:
    name: str
    isClosed: bool

d1 = VideoFile('file1', False)
d2 = VideoFile('file2', False)
d3 = VideoFile('file3', False)
dfiles = [ d1, d2, d3 ]

print(dfiles)
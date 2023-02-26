import os

def get_dirs(path):
    dirs = []
    for dirname, dirnames, filenames in os.walk(path):
        for subdirname in dirnames:
            dirs.append(os.path.join(dirname, subdirname))
    return dirs

print(get_dirs('/home/lixt/Desktop/projects/Lit Engine - engine/main/Engine'))

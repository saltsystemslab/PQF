import shutil
import os

def concatFolder(dir):
    filenames= os.listdir (dir) # get all files' and folders' names in the current directory

    result = []
    for filename in filenames: # loop through all the files and folders
        if os.path.isdir(os.path.join(os.path.abspath(dir), filename)): # check whether the current object is a folder or not
            result.append(filename)
            # print(filename)

    ofolder = dir+"/concated"
    print(ofolder)
    if not os.path.exists(ofolder):
        os.mkdir(ofolder)

    for folder in result:
        nfol = dir + "/" + folder
        if nfol == ofolder:
            continue
        outfilename = ofolder + "/" + folder
        print(outfilename)
        with open(outfilename, 'wb') as outfile:
            # glob.glob('*.txt')
            for filename in os.listdir(nfol):
                # if filename == outfilename:
                #     # don't want to copy the output into the output
                #     continue
                with open(nfol + "/" + filename, 'rb') as readfile:
                    shutil.copyfileobj(readfile, outfile)

def concatMany(depth, dir = "."):
    if depth == 0:
        print("FFAFAF" + " " + dir)
        concatFolder(dir)
        return
    
    folders = os.listdir (dir)
    for folder in folders:
        f = dir + "/" + folder
        if os.path.isdir(f):
            concatMany(depth-1, f)

depth = 0
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("-d", "--depth", type=int)

args = parser.parse_args()

if args.depth:
    depth = args.depth

print(depth)
concatMany(depth)


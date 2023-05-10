import shutil
import os
import math
import matplotlib.pyplot as plt

def plotStatistic(picname, filname, sfunc, all_failures, Ns):
    stat = []
    for N, failures in all_failures:
        stat.append(failures[int(sfunc(len(failures)))] / N)
    Nslog, stat = (list(t) for t in zip(*sorted(zip(Ns, stat))))

    # fig, ax = plt.subplots()
    # ax.plot(Nslog, stat)
    # plt.savefig(picname + ".png")
    # plt.close(fig)

    with open(filname + ".txt", 'w') as f:
        f.write("x_0 y_0\n")
        for (x, y) in zip(Nslog, stat):
            f.write(str(x) + " " + str(y) + "\n")

def analyzeFolder(idir, odir, name):
    folder = idir+"/concated/"
    filenames= os.listdir (folder)

    Ns = []
    # FailureMedians = []
    # Failure1Percent = []
    # FailureMin = []
    all_failures = []

    for file in filenames:
        failures = []
        for line in open(folder + file):
            listWords = line.split(" ")
            # print(listWords)
            failures.append(int(listWords[0]))
        if len(failures) == 0:
            continue
        Ns.append(math.log2(int(file)))
        N = int(file)
        failures.sort()
        all_failures.append((int(file), failures))

    if not os.path.exists(odir):
        os.mkdir(odir)
        os.mkdir(odir+"/medians")
        os.mkdir(odir+"/1pcts")
        os.mkdir(odir+"/mins")
        os.mkdir(odir+"/medianpics")
        os.mkdir(odir+"/1pctpics")
        os.mkdir(odir+"/minpics")
        
    plotStatistic(odir+"/medianpics/" + name, odir+"/medians/" + name, lambda l: l/2, all_failures, Ns)
    plotStatistic(odir+"/1pctpics/" + name, odir+"/1pcts/" + name, lambda l: l/100, all_failures, Ns)
    plotStatistic(odir+"/minpics/" + name, odir+"/mins/" + name, lambda l: 0, all_failures, Ns)

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

def concatMany(depth, odir, idir = ".", folder = ""):
    if depth == 0:
        concatFolder(idir)
        analyzeFolder(idir, odir, folder)
        return
    
    folders = os.listdir (idir)
    for folder in folders:
        if folder == odir:
            continue
        f = idir + "/" + folder
        if os.path.isdir(f):
            concatMany(depth-1, odir, f, folder)

depth = 0
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("-d", "--depth", type=int)
parser.add_argument("-o", "--odir", type=int)

args = parser.parse_args()

if args.depth:
    depth = args.depth

odir = "analysis"
if args.odir:
    odir = args.odir

print(depth)
concatMany(depth, odir)


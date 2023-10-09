import shutil
import os, os.path
import errno
import math

# Taken from https://stackoverflow.com/a/600612/119527
def mkdir_p(path):
    try:
        os.makedirs(path)
    except OSError as exc: # Python >2.5
        if exc.errno == errno.EEXIST and os.path.isdir(path):
            pass
        else: raise

def safe_open_w(path):
    ''' Open "path" for writing, creating any parent directories as needed.
    '''
    mkdir_p(os.path.dirname(path))
    return open(path, 'w')


def plotStastic(filname, Ns, stat):
    with safe_open_w(filname + ".txt") as f:
        f.write("x_0 y_0\n")
        for (x, y) in zip(Ns, stat):
            f.write(str(x) + " " + str(y) + "\n")


#rough idea I think: first, get averages for each lf(sorted by lf of course)
#Top folder is size of filter (kinda inverted from the generation but whatever)
#Each different category gets subfolder
#Each file then is different type of filter
#Then just extrapolate average insert time for each stage by subtracting and dividing, make that one file
#Simply extract averages for the two types of queries, make those two files
#Remove times do same as inserts
#FPR calculate that as two things: FPR versus lf and log(1/FPR) versus size per item at different lfs (or maybe just do it at the highest lf? Not sure, but do this for now)
def analyzeFolder(idir, odir):
    # folder = idir+"/concated/"
    folder = idir
    filenames= os.listdir (folder)

    for ftype in filenames:
        innerfolder = folder + "/" + ftype + "/"
        print(innerfolder)
        if innerfolder == odir or not os.path.isdir(innerfolder):
            continue
        sizenames = os.listdir (innerfolder)
        for file in sizenames:
            N = int(file)
            stats = {}
            for line in open(os.path.join(innerfolder,file)):
                lw = line.split(" ")
                if int(lw[2]) > 1000000000:
                    print("FSFF")
                    continue
                lf = float(lw[0])
                if lf not in stats:
                    stats[lf] = [0, 0, 0, 0, 0, 0, 0, 0]
                stats[lf][0] += int(lw[1])
                stats[lf][1] += int(lw[2])
                stats[lf][2] += int(lw[3])
                stats[lf][3] += int(lw[4])
                stats[lf][4] += int(lw[5])
                stats[lf][5] += float(lw[6])
                stats[lf][6] += int(lw[7])
                stats[lf][7] += 1
                # Ns.append(float(lw[0]))
                # inserts.append(int(lw[1]))
                # squeries.append(int(lw[2]))
                # rqueries.append(int(lw[3]))
                # removals.append(int(lw[4]))
                # fpr.append(float(lw[5]))
                # sizes.append(int(lw[6]))
            pns = 0
            pstat = [0, 0, 0, 0, 0, 0, 0]
            Ns = []
            lfs = []
            inserts = []
            squeries = []
            rqueries = []
            removals = []
            mixed = []
            fpr = []
            sizePerKey = []
            efficiency = []
            # print(stats)
            # print(sorted(stats))
            # print(dict(sorted(stats.items())))
            for ns, s in dict(sorted(stats.items())).items():
                # Ns.append(ns)
                lfs.append(ns)
                itime = (s[0] - pstat[0])/(s[7]*N*(ns-pns))
                inserts.append(1 / itime) #so 1000000 / itime would be #items inserted per second, but we want million items / sec
                sqtime = s[1]/(s[7]*N*ns)
                squeries.append(1/ sqtime)
                rqtime = s[2]/(s[7]*N*ns)
                rqueries.append(1 / rqtime)
                rmtime = (s[3] - pstat[3])/(s[7]*N*(ns-pns))
                if rmtime < .00001 or rmtime > 100000:
                    removals.append(0)
                else:
                    removals.append(1/rmtime)
                mixedTime = s[4]/(s[7]*N*ns)
                if mixedTime < .00001 or mixedTime > 100000:
                    mixed.append(0)
                else:
                    mixed.append(4/mixedTime)
                fpr.append(s[5] / s[7])
                sizePerKey.append(8*s[6] / (s[7]*N*ns))
                if s[5] == 0:
                    print(ftype)
                    print(N)
                    print(ns)
                    efficiency.append(10) #no idea what to do with this this is really odd
                    Ns.append(0)
                else:
                    efficiency.append(math.log2(1/fpr[-1]) / sizePerKey[-1])
                    Ns.append(efficiency[-1])

            upperf = os.path.join(odir,file)

            plotStastic(os.path.join(upperf, "inserts/", ftype), Ns, inserts)
            plotStastic(os.path.join(upperf, "squeries/", ftype), Ns, squeries)
            plotStastic(os.path.join(upperf, "rqueries/", ftype), Ns, rqueries)
            plotStastic(os.path.join(upperf, "removals/", ftype), Ns, removals)
            plotStastic(os.path.join(upperf, "mixed/", ftype), Ns, mixed)
            plotStastic(os.path.join(upperf, "fpr/", ftype), Ns, fpr)
            plotStastic(os.path.join(upperf, "sizes/", ftype), Ns, sizePerKey)
            plotStastic(os.path.join(upperf, "efficiency/", ftype), lfs, efficiency)

# depth = 0
import argparse

parser = argparse.ArgumentParser()
# parser.add_argument("-d", "--depth", type=int)
parser.add_argument("-i", "--idir", type=str)
parser.add_argument("-o", "--odir", type=str)

args = parser.parse_args()

# if args.depth:
#     depth = args.depth

idir = "."
if args.idir:
    idir = args.idir

odir = "analysis"
if args.odir:
    odir = args.odir

analyzeFolder(idir, odir)


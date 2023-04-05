import os
import math

folder = "concated/"
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

import matplotlib.pyplot as plt

def plotStatistic(name, sfunc):
    stat = []
    for N, failures in all_failures:
        stat.append(failures[int(sfunc(len(failures)))] / N)
    Nslog, stat = (list(t) for t in zip(*sorted(zip(Ns, stat))))

    fig, ax = plt.subplots()
    ax.plot(Nslog, stat)
    plt.savefig(name + ".png")

plotStatistic("median2", lambda l: l/2)
plotStatistic("1pct2", lambda l: l/100)
plotStatistic("min2", lambda l: 0)

# FailureMedians.append(failures[int(len(failures)/2)] / N)
#     Failure1Percent.append(failures[int(len(failures)/100)]/ N)
#     FailureMin.append(failures[0]/ N)

# Nslog, FailureMin, FailureMedians, Failure1Percent = (list(t) for t in zip(*sorted(zip(Ns, FailureMin, FailureMedians, Failure1Percent))))

# fig, ax = plt.subplots()
# ax.plot(Nslog, FailureMedians)
# plt.savefig("median.png")
# fig, ax = plt.subplots()
# ax.plot(Nslog, Failure1Percent)
# plt.savefig("1pct.png")
# fig, ax = plt.subplots()
# ax.plot(Nslog, FailureMin)
# plt.savefig("min.png")


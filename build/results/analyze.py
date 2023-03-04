import os
import math

folder = "concated/"
filenames= os.listdir (folder)

Ns = []
FailureMedians = []
Failure1Percent = []
FailureMin = []

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
    FailureMedians.append(failures[int(len(failures)/2)] / N)
    Failure1Percent.append(failures[int(len(failures)/100)]/ N)
    FailureMin.append(failures[0]/ N)

import matplotlib.pyplot as plt

Ns, FailureMin, FailureMedians, Failure1Percent = (list(t) for t in zip(*sorted(zip(Ns, FailureMin, FailureMedians, Failure1Percent))))

fig, ax = plt.subplots()
# ax.plot(Ns, FailureMedians)
# ax.plot(Ns, Failure1Percent)
ax.plot(Ns, FailureMin)
# plt.savefig("median.png")
# plt.savefig("1pct.png")
plt.savefig("min.png")


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

# colors = {0.95: "blue", 1.0: "green", 1.05: "red", 1.10: "orange"}

fig, ax = plt.subplots()
ax.plot(Ns, FailureMedians)
plt.savefig("median.png")
fig, ax = plt.subplots()
ax.plot(Ns, Failure1Percent)
plt.savefig("1pct.png")
fig, ax = plt.subplots()
ax.plot(Ns, FailureMin)
plt.savefig("min.png")


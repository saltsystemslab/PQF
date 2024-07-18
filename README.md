README
# Partition Quotient Filter

## Steps To Build
- Please update the submodules using the command 
```shell
git submodule update --init
```
- Build the PQF files. Please execute the following commands.
```shell
cd test/pqf
git checkout master
git pull
make clean && make THREAD=1
cd ../..
```
### Disclaimer:
While running the single threaded VQF benchmarks, please make VQF without the ```THREAD = 1``` flag.
- Build the Tester file. This is the main file required to run the benchmarks.
```shell
cd build
cmake ..
make clean && make
```

## Steps To Run
Please use the following command format to run the code
```shell
numactl -N 0 -m 0 ./Tester <config_file> <output_file> <output_directory>
```
## Single-Threaded Benchmarks
We provide the following single-threaded benchmarks.
- Standard Benchmark: This benchmark will insert a certain number of keys into the filter, perform queries for the same keys, perform queries for keys not in the filter to measure false positives, and perform deletes if the filter supports it.
- Mixed Workload Benchmark: This benchmark will first insert a certain number of keys into the filter. It will then execute 1M operations consisting of inserts/queries/deletes in the proportion (0.3/0.4/0.3).
- Insert Delete Benchmark: This benchmark will first insert a certain number of keys, then attempt to delete them sequentially, and also at random.
- Load Factor Benchmark: This benchmark will measure how many inserts it takes for a filter to fail at various load factors.
### How To Run Single-Threaded Benchmarks
- In order to run the single-threaded benchmarks, you need to modify the ```ST_Config.txt``` file available in the build folder. The file has the following format.
```text
<Benchmark Name>
NumTrials <Number of trials>
NumReplicants <Number of replicants>
NumKeys <Number of keys>
LoadFactorTicks <number of sections to divide the workload into>
MaxLoadFactor <between 0 and 1>
<Filter name>
```
The ```ST_Config.txt``` file contains configurations which just need to be changed in order to run any benchmark. For example, you will find the following benchmark.
```text
Benchmark
NumKeys 1048576
NumThreads 1
NumTrials 1
NumReplicants 1
LoadFactorTicks 20
MaxLoadFactor 0.83
PQF_8_22
```
You can modify the number of keys, threads, replicants, load factor ticks, max load factor and the filter name. Examples of all of these are provided in the test configuration files. All you need to do is uncomment those. 
An example command of how to run the single threaded tests is:
```shell
numactl -N 0 -m 0 ./Tester ST_Config.txt outST.txt analysis
```

We have provided several examples of such configurations in the configuration files themselves. If you want to run the experiment for a particular filter, please remove the ```#``` before it's name.
## Disclaimer
Please delete the ```outST.txt```/```outMT.txt``` files after running the single/multi-threaded benchmarks. Running with these files present will cause the experiments to not run at all.
The results of all the tests can be found in the path ```build/analysis/<BenchmarkName>/<FilterName>```
## Multithreaded Benchmarks
We provide the following multithreaded benchmarks.
- Multithreaded inserts/queries: This benchmark first measures the time it takes to perform inserts with various number of threads, after which it performs queries and reports the throughput for both.
- Mixed Workload Benchmark: This benchmark functions the same way as the one for single thread. It just executes the benchmark with multiple threads.

These benchmark configurations are found in a file called ```MT_Config.txt```. It can be run the same way as the single threaded tests. For example,
```shell
numactl -N 0 -m 0 ./Tester MT_Config.txt outMT.txt analysis 
```
**Please be advised that running the same benchmark consecutively will erase previous results of the same benchmark.**

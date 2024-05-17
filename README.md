# Dynamic-Prefix-Filter

## How to Run
Make sure to clone the submodules: git submodule update --init

### Build VQF:
- cd test/vqf
- If running single-threaded test: make
- If running multi-threaded test: make THREAD=1
- If already ran make, MAKE SURE TO DO "make clean" first

### Build Tester:
- cd build
- cmake ..
- make

### Run Tester:
- Basic method to run: give config file as first arg, raw test output file, and lasty the *folder* to put the coalesced data. NOTE: THE CODE DELETES files in the analysis(coalesced) folder
- Single threaded example: ./Tester ST_Config.txt outST.txt analysis
- Multi threaded example: ./Tester MT_Config.txt outMT.txt analysis

### All together:

#### Singlethreaded:
cd test/vqf &&
make clean &&
make && 
cd ../.. && 
cd build && 
cmake .. && 
make && 
./Tester ST_Config.txt outST.txt analysis

#### Multithreaded:
cd test/vqf &&
make clean && 
make THREAD=1 && 
cd ../.. && 
cd build && 
cmake .. && 
make && 
./Tester MT_Config.txt outMT.txt analysis

### Potential Compilation Issue
One of the Prefix Filter files has a problem, but for some reason older versions of g++ do not catch this. When I use g++-12, it complains along the lines of:

Dynamic-Prefix-Filter/test/Prefix-Filter/Prefix-Filter/Shift_op.cpp:895:57: error: ‘vector’ in namespace ‘std’ does not name a template type
  895 |     void memcpy_for_vec(uint8_t *pack_array, const std::vector<bool> *b_vec) {

To fix this, basically just add "include &lt;vector&gt;;" to top of the Shift_op.cpp file in test/Prefix-Filter/Prefix-Filter

### Modifying the Config
I will explain this more later, but you can change the values like numkeys or numtrials or loadfactorticks (number of ranges to split inserting/querying into)
Can also write comments starting with #, and the name of a filter causes it to be benchmarked
I have two filters uncommented, one the vqf and another filter for reference

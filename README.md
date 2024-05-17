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

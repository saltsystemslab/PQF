import subprocess
import os
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("-vt", type=bool)
parser.add_argument("-clean", type=bool)

args = parser.parse_args()

VQFthreaded = False
CleanMake = False

if args.vt:
    VQFthreaded = args.vt

if args.clean:
    CleanMake = args.clean


current_path = os.path.dirname(__file__)

relative_vqf_build_path = "../test/vqf"
full_vqf_path = os.path.join(current_path, relative_vqf_build_path)

print("building VQF")
if CleanMake:
    subprocess.run(f"cd {full_vqf_path} && make clean", shell=True)
if not VQFthreaded:
    subprocess.run(f"cd {full_vqf_path} && make", shell=True)
else:
    subprocess.run(f"cd {full_vqf_path} && make THREAD=1", shell=True)


relative_build_path = "../build"
full_build_path = os.path.join(current_path, relative_build_path)

print("building everything else (and linking vqf)")
# print(full_build_path)
if CleanMake:
    subprocess.run(f"cd {full_build_path} && cmake .. && make clean && make", shell=True)
else:
    subprocess.run(f"cd {full_build_path} && cmake .. && make", shell=True)


# import subprocess

# subprocess.run(["cd", "../build", "&", "ls"], shell=True, capture_output=True)
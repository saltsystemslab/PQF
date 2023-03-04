import shutil
import os

filenames= os.listdir (".") # get all files' and folders' names in the current directory

result = []
for filename in filenames: # loop through all the files and folders
    if os.path.isdir(os.path.join(os.path.abspath("."), filename)): # check whether the current object is a folder or not
        result.append(filename)
        # print(filename)

ofolder = "concated"

for folder in result:
    if folder == ofolder:
        continue
    outfilename = ofolder + "/" + folder
    with open(outfilename, 'wb') as outfile:
        # glob.glob('*.txt')
        for filename in os.listdir(folder):
            # if filename == outfilename:
            #     # don't want to copy the output into the output
            #     continue
            with open(folder + "/" + filename, 'rb') as readfile:
                shutil.copyfileobj(readfile, outfile)

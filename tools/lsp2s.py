import zipfile,sys
z=zipfile.ZipFile(sys.argv[1])
for i in z.infolist(): print(i.filename,i.file_size,i.compress_type)

#!/bin/bash

mkdir test
cp -r ./code ./test/code
cat ./code/14.c

gcc -c ./test/code/0.c
gcc -c ./test/code/1.c
gcc -c ./test/code/2.c
gcc -c ./test/code/3.c
gcc -c ./test/code/4.c
gcc -c ./test/code/5.c
gcc -c ./test/code/6.c
gcc -c ./test/code/7.c
gcc -c ./test/code/8.c
gcc -c ./test/code/9.c
gcc -c ./test/code/10.c
gcc -c ./test/code/11.c
gcc -c ./test/code/12.c
gcc -c ./test/code/13.c
gcc -c ./test/code/14.c
gcc -c ./test/code/15.c

gcc -o ./test/hello ./test/code/*.o

chmod +x ./test/hello
./test/hello 2> ./test/err.txt

mv ./test/err.txt ./
chmod ./err.txt rw-r-xr-x

n=2
if [ $# -eq 0 ] then
	n=2
elif [ $# -eq 1 ] then
	n=$1+1
else then
	n=$1+$2
fi

sed -n "$np" ./err.txt >&2

#!/bin/bash

mkdir test
cp -r ./code ./test/code
cat ./code/14.c

cd ./test/code
gcc -c ./*.c
cd ../..

gcc -o ./test/hello ./test/code/*.o

chmod +x ./test/hello
./test/hello 2> ./test/err.txt

mv ./test/err.txt ./
chmod 655 ./err.txt

n=2
if [ $# -eq 0 ]; then
	n=2
elif [ $# -eq 1 ]; then
	num=$1
	n=$((num+1))
else
	n1=$1
	n2=$2
	n=$((n1+n2))
fi

sed -n "$np" ./err.txt >&2

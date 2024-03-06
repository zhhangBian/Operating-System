#!/bin/bash
#First you can use grep (-n) to find the number of lines of string.
#Then you can use awk to separate the answer.
input=$1
output=$3
pattern=$2
touch "$output"
touch tmp
chmod a+w tmp
chmod a+w "$output"
grep -n "$pattern" "$input" > tmp
sed 's/:'$pattern'//g' tmp | awk '{print $1}' > "$output"
rm tmp

#!/bin/bash
file=$1
pre=$2
aft=$3
sed -i 's/'$pre'/'$aft'/g' "$file"

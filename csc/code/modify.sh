#!/bin/bash
file=$1
pre=$2
aft=$3
sed 's/'$pre'/'$after'/g' "$file"

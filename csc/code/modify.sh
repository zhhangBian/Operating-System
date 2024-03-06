#!/bin/bash
file=$1
pre=$2
aft=$3
sed 's/'$2'/'$3'/g' "$1"

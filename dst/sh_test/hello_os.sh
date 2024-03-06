#!/bin/bash
nameA=$1
nameB=$2
sed -n '8p;32p;128p;512p;1024p' "$nameA" > "$nameB"

#!/bin/bash

filename="digits.txt"

rm -f $filename

for((i=-1000; i < 1000; i++)) ; do
    echo $i >> $filename
done

for((i=500000000, j=20; j > 0; j--, i-=999)) ; do
    echo $i >> $filename
done

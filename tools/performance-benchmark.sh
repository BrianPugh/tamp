#!/bin/bash

dir="build/silesia"

for file in "$dir"/*; do
    filename=$(basename "$file")
    fullpath="$dir/$filename"
    raw_count=$(cat $fullpath | wc -c)
    tamp_count=$(tamp compress $fullpath | wc -c)
    zlib_count=$(python tools/zlib_compress.py $fullpath | wc -c)
    hs_count=$(../heatshrink/heatshrink -w 10 -e $fullpath | wc -c)
    printf "| %s | %'d | %'d | %'d | %'d |\n" $fullpath $raw_count $tamp_count $zlib_count $hs_count
done

#!/bin/sh
if [ $# -lt 2 ]; then
    echo "$0 input_file output_file"
    exit 1
fi

if [ ! -f $1 ]; then
    echo "File $1 does not exist"
    exit 1
fi

cat $1 > $2

grep -o '#include \".*\"' $1 | while read line
do
    incl_file=${line#\#include \"}
    incl_file=${incl_file%\"}
    incl_file_path="$(dirname $1)/$incl_file"

    if [ ! -f $incl_file_path ]; then
        echo "File $incl_file included from $1 does not exist"
        exit 1;
    fi

    awk -v match_str=$incl_file -v new_file=$incl_file_path '{
        if(index($0, match_str))
        {
            system("cat " new_file);
            print "";
            next;
        } else {
            print
        }
    }' $2 > $2.tmp
    mv $2.tmp $2
done

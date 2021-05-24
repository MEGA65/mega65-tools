#!/bin/sh

# very simple test runner: find all *.prg files in the current directory
# and run them.
#
# todo: 
# - detect c64 or c65 mode programs
# - adapt existing tests to make them aware of the test framework (ugh...)

for file in *.prg; do 
    if [ -f "$file" ]; then 
        echo "$file" 
        ../../bin/m65 -Fur4 $file -w ./tests.log
    fi 
done
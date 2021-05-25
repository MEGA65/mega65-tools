#!/bin/bash

# very simple test runner: find all *.prg files in the current directory
# and run them.
#
# todo: 
# - adapt existing tests to make them aware of the test framework (ugh...)

c64runopts="-Fur4"
c65runopts="-Fur"

for file in *.prg; do
    if [ -f "$file" ]; then 
        addr=$(hexdump -n 2-x $file -e '"%02X"')
        if [[ "$addr" == "801" ]]; then 
          opts=$c64runopts
        elif [[ "$addr" == "2001" ]]; then 
          opts=$c65runopts
        else
          echo "ERROR: unknown start addresss in file $file"
          exit -1
        fi
        ../../bin/m65 $opts $file -w ./tests.log
    fi 
done
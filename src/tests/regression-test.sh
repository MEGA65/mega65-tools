#!/bin/bash

SCRIPT="$(readlink --canonicalize-existing "$0")"
SCRIPTPATH="$(dirname "${SCRIPT}")"
SCRIPTNAME=${SCRIPT##*/}

usage () {
    echo "Usage: ${SCRIPTNAME} BITSTREAM LOGPATH [DEVICE]"
    if [[ "x$1" != "x" ]]; then
        echo
        echo $1
    fi
    exit 1
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
    usage
fi

DEFAULT_TIMEOUT=60

BITSTREAM=$1
LOGPATH=$2
DEVICE=
if [[ $# -eq 3 ]]; then
    if [[ ! -e $3 ]]; then
        usage "device $3 does not exist"
    fi
    DEVICE="-l $3"
fi
if [[ ! -e ${BITSTREAM} ]]; then
    usage "bitstream does not exist"
fi
if [[ ! -d ${LOGPATH} ]]; then
    usage "logpath does not exist or is not directory"
fi

# extract bitstream name for logfiles
LOGNAME=${BITSTREAM%%.bit}
LOGNAME=${LOGNAME##*/}

declare -i FAILED; FAILED=0
declare -i COUNT; COUNT=0
declare -i fail

parse_test_log () {
    local logfile=$1
    if [[ ! -e ${logfile} ]]; then
        echo "  Logfile is missing!"
        echo "  Test result: failed"
        FAILED+=1
        return
    fi
    # parse bitstream version
    temp=$(grep "===== BITSTREAM: " $logfile)
    temp=${temp##*BITSTREAM: }
    temp=${temp#*,} # cut branch
    local test_bs_date=${temp%,*}
    local test_bs_hash=${temp#*,}
    # parse test name
    temp=$(grep START $logfile)
    temp=${temp##*START (}
    local test_name=${temp%%)}
    # parse fail
    temp=$(grep 'FAILCOUNT:' $logfile)
    local test_fails=${temp##*FAILCOUNT: }
    # parse TIMEOUT
    local test_timeout=0
    if [[ $(grep -c '!!!!! TIMEOUT' $logfile > /dev/null) ]]; then
        test_timeout=1
    fi

    # Result
    fail=0
    echo "  Test name: ${test_name}"
    if [[ !(${BITSTREAM} =~ ${test_bs_date}) || !(${BITSTREAM} =~ ${test_bs_hash}) ]]; then
        echo "  Bitstream check: failed (${test_bs_date}-${test_bs_hash} found)";
        fail=1
    else
        echo "  Bitstream check: ok"
    fi
    if [[ $test_fails -eq 0 && $test_timeout -eq 0 ]]; then
        echo "  Test result: success"
    elif [[ $test_timeout -eq 1 ]]; then
        echo "  Test result: timeout"
        fail=1
    else
        echo "  Test result: failed (${test_fails} failures)"
        fail=1
    fi
    FAILED+=fail
}

main () {
    echo "Running regression tests for bitstream"
    echo "  ${BITSTREAM}"
    echo

    while read -r test timeout; do
        # skip comments and empty lines
        if [[ $test =~ ^# || -z $test ]]; then
            continue
        fi
        # check if timeout is an number or use default timeout
        if [[ !($timeout =~ ^[0-9]+$) ]]; then
            timeout=$DEFAULT_TIMEOUT
        fi
        utlog="${LOGPATH}/${LOGNAME}.${test%%.prg}.log"
        if [[ -e ${utlog} ]]; then
            rm ${utlog}
        fi
        echo "running ${test}..."
        ${SCRIPTPATH}/../../bin/m65 ${DEVICE} --bit "${BITSTREAM}" --c64mode --run --unittest=$timeout --utlog ${utlog} "${SCRIPTPATH}/${test}" >& /dev/null
        parse_test_log ${utlog}
        COUNT+=1
    done < ${SCRIPTPATH}/regression-tests.lst

    echo
    echo "Executed ${COUNT} tests for bitstream ${BITSTREAM##*/} with ${FAILED} failed."

    exit $FAILED
}

set -o pipefail
main | tee ${LOGPATH}/${LOGNAME}.tests.log

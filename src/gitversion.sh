#!/bin/bash

# status of branch in short format
# work around travis and jenkins doing things differently
if [[ -n $JENKINS_SERVER_COOKIE ]]; then
    branch2=${BRANCH_NAME:0:6}
    version=${GIT_COMMIT:0:7}
    buildnum=${BUILD_NUMBER}
else
    if [[ -n $TRAVIS_BRANCH ]]; then
        branch=$TRAVIS_BRANCH
    else
        branch=$(git rev-parse --abbrev-ref HEAD)
    fi
    # we only take the first 6 chars
    branch2=${branch:0:6}
    # get commit (7 chars plus optional ~ for dirty)
    version=$(git describe --always --abbrev=7 --dirty=~)
    buildnum=man
fi
# get date plus hour
datetime=$(date +%Y%m%d.%H)

if [[ $1 = arcname ]]; then
    echo "${branch2}-${buildnum}-${version:0:6}"
else
    echo "${datetime}-${branch2}-${version}"
fi

#!/bin/bash

shorten_name () {
  name=$1
  # issue branch?
  if [[ $name =~ ^([0-9]+)-(.+)$ ]]; then
    name="${BASH_REMATCH[1]}${BASH_REMATCH[2]}"
  fi
  # developemnt + extra?
  if [[ $name =~ ^development-(.+)$ ]]; then
    name="dev-${BASH_REMATCH[1]}"
  fi
  # release + version?
  if [[ $name =~ ^release-([0-9])\.([0-9][0-9]?)$ ]]; then
    name="r-${BASH_REMATCH[1]}.${BASH_REMATCH[2]}000"
  fi
  # cut after 6 chars
  echo ${name:0:6}
}


if [[ -n $JENKINS_SERVER_COOKIE ]]; then
  branch=${BRANCH_NAME}
  version=${GIT_COMMIT:0:7}
  buildnum=${BUILD_NUMBER}
else
  if [[ -n $TRAVIS_BRANCH ]]; then
    branch=$TRAVIS_BRANCH
    buildnum=$TRAVIS_BUILD_NUMBER
  else
    branch=`git rev-parse --abbrev-ref HEAD`
    buildnum=man
  fi
  # get commit (7 chars plus optional ~ for dirty)
  version=$(git describe --always --abbrev=7 --dirty=~)
fi
branch2=$(shorten_name $branch)

# get date plus hour
datetime=$(date +%Y%m%d.%H)

if [[ $1 = arcname ]]; then
    echo "${branch2}-${buildnum}-${version:0:6}"
elif [[ $1 = devrelext ]]; then
    if [[ ${branch} =~ ^release-.+$ ]]; then
      echo "rel"
    else
      echo "dev"
    fi
else
    echo "${datetime}-${branch2}-${version}"
fi

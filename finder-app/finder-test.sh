#!/bin/sh
# Tester script for assignment 4
# Author: Mouin EDDEB

set -e
set -u

NUMFILES=10
WRITESTR=AELD_IS_FUN
WRITEDIR=/tmp/aeld-data
username=$(cat /etc/finder-app/conf/username.txt)  # Absolute path for config

if [ $# -lt 3 ]; then
    echo "Using default value ${WRITESTR} for string to write"
    if [ $# -lt 1 ]; then
        echo "Using default value ${NUMFILES} for number of files to write"
    else
        NUMFILES=$1
    fi
else
    NUMFILES=$1
    WRITESTR=$2
    WRITEDIR=/tmp/aeld-data/$3
fi

MATCHSTR="The number of files are ${NUMFILES} and the number of matching lines are ${NUMFILES}"
assignment=$(cat /etc/finder-app/conf/assignment.txt)  # Absolute path for config

echo "Writing ${NUMFILES} files containing string ${WRITESTR} to ${WRITEDIR}"

rm -rf "${WRITEDIR}"

if [ $assignment != 'assignment1' ]; then
    mkdir -p "$WRITEDIR"
    if [ -d "$WRITEDIR" ]; then
        echo "$WRITEDIR created"
    else
        exit 1
    fi
fi

for i in $(seq 1 $NUMFILES); do
    writer "$WRITEDIR/${username}$i.txt" "$WRITESTR"  # Remove ./
done

OUTPUTSTRING=$(finder.sh "$WRITEDIR" "$WRITESTR")  # Remove ./

# Write result to required file
echo "$OUTPUTSTRING" > /tmp/assignment4-result.txt

# Cleanup
rm -rf /tmp/aeld-data

set +e
echo "${OUTPUTSTRING}" | grep "${MATCHSTR}"
if [ $? -eq 0 ]; then
    echo "success"
    exit 0
else
    echo "failed: expected ${MATCHSTR} in ${OUTPUTSTRING} but instead found"
    exit 1
fi
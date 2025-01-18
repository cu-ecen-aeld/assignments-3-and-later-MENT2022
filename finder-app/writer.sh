#!/bin/bash

# Check if both arguments are provided
if [ $# -ne 2 ]; then
    echo "Error: Two arguments required"
    echo "Usage: $0 <writefile> <writestr>"
    exit 1
fi

writefile=$1
writestr=$2

# Create directory path if it doesn't exist
dir_path=$(dirname "$writefile")
mkdir -p "$dir_path"

# Write string to file
if ! echo "$writestr" > "$writefile"; then
    echo "Error: Could not create file $writefile"
    exit 1
fi
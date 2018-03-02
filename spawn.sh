#!/bin/bash

client=$1
num=$2

echo "Spawning $num clients.."

for((i = 1; i <= num; i++)); do
  ( ./$client & )
done

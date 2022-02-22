#!/bin/bash
killall websockify > /dev/null 2>&1
killall roid.d > /dev/null 2>&1
websockify localhost:8001 localhost:9000 >> websockify.log 2>&1 &
./build/roid.d >> roid.log 2>&1 &
sleep 1
ps -a | grep websockify
ps -a | grep roid.d


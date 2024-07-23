#!/bin/bash
killall websockify > /dev/null 2>&1
killall roid.d > /dev/null 2>&1
sleep 1
ps -a | grep websockify
ps -a | grep roid.d


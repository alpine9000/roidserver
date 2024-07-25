#!/bin/bash
killall websockify > /dev/null 2>&1
websockify localhost:8011 localhost:9000 >> websockify.log 2>&1



#!/bin/bash

docker build --build-arg HOST_UID=$(id -u) --progress-plain -t my-buildroot-env .

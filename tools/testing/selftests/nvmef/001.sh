#!/bin/bash
# Exercises configure target with a not-exist block device
# and then remove the namespace.
# commit 832ef2d fixed the crash.

. ./nvmf_lib.sh

NAME=selftest-nvmf
TARGET_DEVICE=/dev/not-exist

nvmf_target ${NAME} ${TARGET_DEVICE}
nvmf_cleanup_target ${NAME}

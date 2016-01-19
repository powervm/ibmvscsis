#!/bin/bash
# Copyright (c) 2015 PMC and/or its affiliates. All Rights Reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or (at your option) any later version.
#
# This program is distributed in the hope that it would be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
# GNU General Public License for more details.
#
# Author: stephen.bates@pmcs.com <stephen.bates@pmcs.com>

set -e
. ./nvmf_lib.sh

NAME=selftest-nvmf
TARGET_DEVICE=/dev/nullb0
NQN=${NAME}

DD_COUNT=1M
DD_BS=512
DD_DIRECT=FALSE
DD_SYNC=FALSE

CLEANUP_ONLY=FALSE
CLEANUP_SKIP=FALSE

nvmf_help()
{
    echo $0 ": Help and Usage"
    echo
    echo "A testing tool for NVMe over Fabrics (NVMf)"
    echo
    echo "usage: $0 [options]"
    echo
    echo "Options"
    echo "-------"
    echo
    echo "  -h             : Show this help message"
    echo "  -n NAME        : Controller name on target side"
    echo "  -t TARGET      : Block device to use on target side"
    echo "  -c COUNT       : Number of IO to test with"
    echo "  -b BS          : IO block size"
    echo "  -d             : Perform direct IO"
    echo "  -s             : Perform synchronous IO"
    echo "  -x             : Just perform a module cleanup"
    echo "  -y             : Do not perform a cleanup"
    echo
}

while getopts "hn:t:c:b:dsxy" opt; do
    case "$opt" in
	h)  nvmf_help
	    exit 0
	    ;;
	n)  NAME=${OPTARG}
            ;;
	t)  TARGET_DEVICE=${OPTARG}
            ;;
	c)  DD_COUNT=${OPTARG}
            ;;
	b)  DD_BS=${OPTARG}
            ;;
	d)  DD_DIRECT=TRUE
            ;;
	s)  DD_SYNC=TRUE
            ;;
	x)  CLEANUP_ONLY=TRUE
            ;;
	y)  CLEANUP_SKIP=TRUE
            ;;
	\?)
	    echo "Invalid option: -$OPTARG" >&2
	    exit 1
	    ;;
	:)
	    echo "Option -$OPTARG requires an argument." >&2
	    exit 1
	    ;;
    esac
done

echo "-----------------"
echo "running nvmf_loop"
echo "-----------------"

  # XXXXX. For now we assume the DUT in a fresh state with none of the
  # relevant modules loaded. We will add checks for this to the script
  # over time.

nvmf_check_cleanup_only $NAME
nvmf_check_configfs_mount
DD_FLAGS=$(nvmf_setup_dd_args $DD_COUNT $DD_BS $DD_DIRECT $DD_SYNC)
nvmf_check_target_device ${TARGET_DEVICE}
nvmf_trap_exit

  # Setup the NVMf target and host.

nvmf_target ${NAME} ${TARGET_DEVICE}

HOST_CTRL=$(nvmf_loop_host ${NQN})
HOST_CHAR=/dev/${HOST_CTRL}
HOST_DEVICE=/dev/${HOST_CTRL}n1

  # Ensure host mapped drive exists

if [ ! -b "${HOST_DEVICE}" ]
then
    echo nvmf: Error creating host device.
    exit -1
fi

  # run some simple tests

echo "testing target directly:"
nvmf_run_dd ${TARGET_DEVICE} $DD_FLAGS
echo
echo
echo "testing via loopback:"
nvmf_run_dd ${HOST_DEVICE} $DD_FLAGS
echo
echo

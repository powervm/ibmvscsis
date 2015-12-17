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

CONFIGFS=/sys/kernel/config

NAME=selftest-nvmf
TARGET_DEVICE=/dev/nullb0
NQN=${NAME}

HOST_CTRL=nvme0

DD_COUNT=1M
DD_BS=512
DD_DIRECT=FALSE
DD_SYNC=FALSE

CLEANUP_ONLY=FALSE
CLEANUP_SKIP=FALSE

while getopts "hn:t:o:c:b:dsxy" opt; do
    case "$opt" in
	h)  nvmf_help
	    exit 0
	    ;;
	n)  NAME=${OPTARG}
            ;;
	t)  TARGET_DEVICE=${OPTARG}
            ;;
	o)  HOST_CTRL=${OPTARG}
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

HOST_CHAR=/dev/${HOST_CTRL}
HOST_DEVICE=/dev/${HOST_CTRL}n1

echo "-----------------"
echo "running nvmf_loop"
echo "-----------------"

  # XXXXX. For now we assume the DUT in a fresh state with none of the
  # relevant modules loaded. We will add checks for this to the script
  # over time.

  # If requested just run the cleanup function. Useful if a previous
  # run has left the system in a undefined state.

if [ "${CLEANUP_ONLY}" == TRUE ]
then
    echo nvmf: Just performing cleanup.
    nvmf_cleanup ${NAME} ${HOST_CTRL}
    exit -1
fi

  # If mountpoint exists use it to ensure that configfs is already
  # mounted.

if [ $(which mountpoint) ]
then
    if !(mountpoint -q ${CONFIGFS})
    then
	echo nvmf: configfs is not mounted.
	exit -1
    fi
else
    echo nvmf: Warning: automount not \
	 found - skipping configfs check.
fi

  # Ensure host device does not already exist.

if [ -e "${HOST_CHAR}" ]
then
    echo nvmf: Error: Host device already exists.
    exit -1
fi

  # Setup the dd iflag and oflag based on user input.

if [ "${DD_DIRECT}" == TRUE ]
then
    if [ "${DD_SYNC}" == TRUE ]
    then
	DD_IFLAG="iflag=direct,sync"
	DD_OFLAG="oflag=direct,sync"

    else
	DD_IFLAG="iflag=direct"
	DD_OFLAG="oflag=direct"
    fi
else
    if [ "${DD_SYNC}" == TRUE ]
    then
	DD_IFLAG="iflag=sync"
	DD_OFLAG="oflag=sync"
    fi
fi

  # Set up the target block device. Note that in-reality any block
  # device can be supported here. If the device is the null_blk device
  # then we create it here if it does not already exist.

if [[ "${TARGET_DEVICE}" == "/dev/nullb"* ]]
then
    if [ ! -b "${TARGET_DEVICE}" ]
    then
	modprobe null_blk
    fi
fi

if [ ! -b "${TARGET_DEVICE}" ]
then
    echo nvmf: Error: Could not find \
	 target device.
    exit -1
fi

  # Setup the NVMf target and host. Also add the loopback kernel
  # module since this is a loopback test.

nvmf_target ${NAME} ${TARGET_DEVICE}
modprobe nvme-loop

nvmf_host ${NQN}

  # Ensure host mapped drive exists

if [ ! -b "${HOST_DEVICE}" ]
then
    echo nvmf: Error creating host device.
    exit -1
fi

  # run some simple tests

echo "testing target directly (reads)..."
dd if=${TARGET_DEVICE} of=/dev/null bs=${DD_BS} count=${DD_COUNT} ${DD_IFLAG}
echo "testing target directly (writes)..."
dd if=/dev/zero of=${TARGET_DEVICE} bs=${DD_BS} count=${DD_COUNT} ${DD_OFLAG}
echo
echo
echo "testing via loopback (reads)..."
dd if=${HOST_DEVICE} of=/dev/null bs=${DD_BS} count=${DD_COUNT} ${DD_IFLAG}
echo "testing via loopback (writes)..."
dd if=/dev/zero of=${HOST_DEVICE} bs=${DD_BS} count=${DD_COUNT} ${DD_OFLAG}
echo
echo
sync

  # Cleanup the system to its initial state

if [ "${CLEANUP_SKIP}" == FALSE ]
then
    nvmf_cleanup ${NAME} ${HOST_CTRL}
fi

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
# Author: Logan Gunthorpe <logang@deltatee.com>

set -e
. ./nvmf_lib.sh

NAME=selftest-nvmf-rdma
TARGET_DEVICE=/dev/nullb0
TARGET_HOST=
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
    echo "  -t TARGET_BLK  : Block device to use on target side"
    echo "  -T TARGET_HOST : Hostname or IP of target side"
    echo "  -c COUNT       : Number of IO to test with"
    echo "  -b BS          : IO block size"
    echo "  -d             : Perform direct IO"
    echo "  -s             : Perform synchronous IO"
    echo "  -x             : Just perform a module cleanup"
    echo "  -y             : Do not perform a cleanup"
    echo
}

while getopts "hn:t:T:c:b:dsxy" opt; do
    case "$opt" in
	h)  nvmf_help
	    exit 0
	    ;;
	n)  NAME=${OPTARG}
            ;;
	t)  TARGET_DEVICE=${OPTARG}
            ;;
	T)  TARGET_HOST=${OPTARG}
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
echo "running nvmf_rdma"
echo "-----------------"

if [ "${TARGET_HOST}" == "" ]; then
    echo "nmvf: No target host specified. Use the -T option."
    exit 1
fi

  # XXXXX. For now we assume the DUT in a fresh state with none of the
  # relevant modules loaded. We will add checks for this to the script
  # over time.

nvmf_check_cleanup_only $NAME
DD_FLAGS=$(nvmf_setup_dd_args $DD_COUNT $DD_BS $DD_DIRECT $DD_SYNC)

CONNECTION=$(ssh ${TARGET_HOST} echo \$SSH_CONNECTION)
REMOTE_NODE=$(ssh ${TARGET_HOST} uname -n)
REMOTE_KERNEL=$(ssh ${TARGET_HOST} uname -r)
CARGS=( $CONNECTION )
REMOTE_IP=${CARGS[2]}
LOCAL_IP=${CARGS[0]}
echo "Remote Address: ${REMOTE_IP} ($REMOTE_NODE)"
echo "Remote Device:  ${TARGET_DEVICE}"
echo "Remote Kernel:  ${REMOTE_KERNEL}"
echo
echo "Local Address:  ${LOCAL_IP} ($(uname -n))"
echo "Local Kernel:   $(uname -r)"
echo

nvmf_trap_exit

  # Setup the NVMf target and host.

nvmf_remote_cmd ${TARGET_HOST} nvmf_check_configfs_mount
nvmf_remote_cmd ${TARGET_HOST} nvmf_check_target_device ${TARGET_DEVICE}
nvmf_remote_cmd ${TARGET_HOST} nvmf_rdma_target ${NAME} ${TARGET_DEVICE}

HOST_CTRL=$(nvmf_rdma_host ${NQN} ${REMOTE_IP} 1023)

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
nvmf_remote_cmd ${TARGET_HOST} nvmf_run_dd ${TARGET_DEVICE} ${DD_FLAGS}
echo
echo
echo "testing over rdma:"
nvmf_run_dd ${HOST_DEVICE} ${DD_FLAGS}
echo
echo

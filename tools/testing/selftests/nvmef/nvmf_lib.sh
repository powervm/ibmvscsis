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
    echo "  -o HOST        : Char device to create on host side"
    echo "  -c COUNT       : Number of IO to test with"
    echo "  -b BS          : IO block size"
    echo "  -d             : Perform direct IO"
    echo "  -s             : Perform synchronous IO"
    echo "  -x             : Just perform a module cleanup"
    echo "  -y             : Do not perform a cleanup"
    echo
}

nvmf_target()
{
    modprobe nvmet

    sleep 1 # Need this to avoid race

    mkdir -p "/sys/kernel/config/nvmet/subsystems/$1"
    mkdir -p "/sys/kernel/config/nvmet/subsystems/$1/namespaces/1"
    echo -n $2 > \
	 "/sys/kernel/config/nvmet/subsystems/$1/namespaces/1/device_path"

}

nvmf_host()
{
    modprobe nvme
    echo "transport=loop,nqn=$1" > /sys/class/nvme-fabrics/ctl/add_ctrl
}

  # Note we always want nvme_cleanup to try all the rmmods so we wrap
  # it in a set +/-e.

nvmf_cleanup_host()
{
    set +e
    echo > /sys/class/nvme/$1/delete_controller
    modprobe -r nvme-loop
    modprobe -r nvme
    modprobe -r nvme_fabrics
    set -e
}

nvmf_cleanup_target()
{
    set +e
    rmdir /sys/kernel/config/nvmet/subsystems/$1/namespaces/1
    rmdir /sys/kernel/config/nvmet/subsystems/$1
    modprobe -r nvmet
    set -e
}

nvmf_cleanup()
{
    nvmf_cleanup_host $2
    nvmf_cleanup_target $1
}

nvmf_cleanup_force()
{
    set +e
    echo > /sys/class/nvme/$1/delete_controller
    modprobe -r nvme-loop
    modprobe -r nvme_fabrics
    modprobe -r nvme
    rmmod -f nvmet
    set -e
}

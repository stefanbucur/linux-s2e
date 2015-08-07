#!/bin/bash
#
# Copyright 2015 EPFL. All rights reserved.

# This requires a 32-bit environment. If running Ubuntu 64-bit, check
# out this: https://help.ubuntu.com/community/DebootstrapChroot

set -e
set -x

S2E_DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
BASE_DIR=${S2E_DIR}/../

cd ${BASE_DIR}

test -d debian || svn export svn://svn.debian.org/svn/kernel/dists/wheezy/linux/debian

make -f debian/rules clean
make -f debian/rules source

sed -i 's/^\(abiname: .*\)-s2e/\1/' debian/config/defines
sed -i 's/^\(abiname: .*\)/\1-s2e/' debian/config/defines

make -f debian/rules.gen setup_i386_none_486

cp s2e/s2e-config debian/build/build_i386_none_486/.config

fakeroot make -f debian/rules.gen binary-arch_i386_none_486 -j$(nproc)

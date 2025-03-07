#!/bin/bash -
# libguestfs
# Copyright (C) 2014 Red Hat Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

# Regression test for:
# https://bugzilla.redhat.com/show_bug.cgi?id=1091803
# tar-in API didn't cancel the receive correctly along all error paths.

source ./functions.sh
set -e
set -x

skip_if_skipped

guestfish <<EOF
scratch 100M
run
mkfs ext2 /dev/sda
mount /dev/sda /
-tar-in nosuchtarfile.tar /nosuchdirectory
# Appliance has now crashed, so any subsequent command fails:
ping-daemon
EOF

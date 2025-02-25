#!/usr/bin/env python3
# Copyright 2018 VMware, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
""" Virtual environment which models a simple bridge with n attached
    interfaces. The bridge runs in a completely isolated namespace.
    Allows the loading and testing of eBPF programs. """

import sys
import logging
from pathlib import Path

# Append tools to the import path.
FILE_DIR = Path(__file__).resolve().parent
# Append tools to the import path.
sys.path.append(str(FILE_DIR.joinpath("../../../tools")))
import testutils


class Bridge:

    def __init__(self, namespace):
        self.ns_name = namespace # identifier of the namespace
        self.br_name = "core"    # name of the central bridge
        self.br_ports = []       # list of the veth pair bridge ports
        self.edge_ports = []     # list of the veth pair edge ports

    def ns_init(self):
        """Initialize the namespace."""
        cmd = f"ip netns add {self.ns_name}"
        errmsg = f"Failed to create namespace {self.ns_name} :"
        result = testutils.exec_process(cmd, errmsg).returncode
        self.ns_exec("ip link set dev lo up")
        return result

    def ns_del(self):
        """Delete the namespace and with it all the process running in it."""
        cmd = f"ip netns pids {self.ns_name} | xargs -r kill; ip netns del {self.ns_name}"
        errmsg = f"Failed to delete namespace {self.ns_name} :"
        return testutils.exec_process(cmd, errmsg).returncode

    def get_ns_prefix(self):
        """Return the command prefix for the namespace of this bridge class."""
        return f"ip netns exec {self.ns_name}"

    def ns_exec(self, cmd_string):
        """Run and execute an isolated command in the namespace."""
        prefix = self.get_ns_prefix()
        # bash -c allows us to run multiple commands at once
        cmd = f'{prefix} bash -c "{cmd_string}"'
        errmsg = f"Failed to run command {cmd} in namespace {self.ns_name}:"
        return testutils.exec_process(cmd, errmsg).returncode

    def ns_proc_open(self):
        """Open a bash process in the namespace and return the handle"""
        cmd = self.get_ns_prefix() + " /usr/bin/env bash "
        return testutils.open_process(cmd)

    def ns_proc_write(self, proc, cmd):
        """Allows writing of a command to a given process. The command is NOT
        yet executed."""
        testutils.log.info(f"Writing {cmd} ")
        try:
            proc.stdin.write(cmd)
        except IOError as e:
            testutils.log.error(f"Error while writing to process\n{e}")
            return testutils.FAILURE
        return testutils.SUCCESS

    def ns_proc_append(self, proc, cmd):
        """Append a command to an open process."""
        return self.ns_proc_write(proc, " && " + cmd)

    def ns_proc_close(self, proc):
        """Close and actually run the process in the namespace. Returns the
        exit code."""
        testutils.log.info("Executing command.")
        errmsg = f"Failed to execute the command sequence in namespace {self.ns_name}"
        return testutils.run_process(proc, testutils.TIMEOUT, errmsg)

    def _configure_bridge(self, br_name):
        """Set the bridge active. We also disable IPv6 to
        avoid ICMPv6 spam."""
        # We do not care about failures here
        self.ns_exec(f"ip link set dev {br_name} up")
        self.ns_exec(f"ip link set dev {br_name} mtu 9000")
        # Prevent the broadcasting of ipv6 link discovery messages
        self.ns_exec("sysctl -w net.ipv6.conf.all.disable_ipv6=1")
        self.ns_exec("sysctl -w net.ipv6.conf.default.disable_ipv6=1")
        # Also filter igmp packets, -w is necessary because of a race condition
        self.ns_exec("iptables -w -A OUTPUT -p 2 -j DROP")
        return testutils.SUCCESS

    def create_bridge(self):
        """Create the central bridge of the environment and configure it."""
        result = self.ns_exec(f"ip link add {self.br_name} type bridge")
        if result != testutils.SUCCESS:
            return result
        return self._configure_bridge(self.br_name)

    def _configure_bridge_port(self, port_name):
        """Set a bridge port active."""
        cmd = f"ip link set dev {port_name} up"
        cmd += f" && ip link set dev {port_name} mtu 9000"
        return self.ns_exec(cmd)

    def attach_interfaces(self, num_ifaces):
        """Attach and initialize n interfaces to the central bridge of the
        namespace."""
        for index in range(num_ifaces):
            edge_veth = str(index)
            bridge_veth = f"br_{index}"
            result = self.ns_exec(f"ip link add {edge_veth} type veth peer name {bridge_veth}")
            if result != testutils.SUCCESS:
                return result
            # result = self.ns_exec("ip link set %s master %s" %
            #                       (edge_veth, self.br_name))
            # if result != testutils.SUCCESS:
            #     return result
            result = self._configure_bridge_port(edge_veth)
            if result != testutils.SUCCESS:
                return result
            result = self._configure_bridge_port(bridge_veth)
            if result != testutils.SUCCESS:
                return result
            # add interfaces to the list of existing bridge ports
            self.br_ports.append(bridge_veth)
            self.edge_ports.append(edge_veth)
        return testutils.SUCCESS

    def create_virtual_env(self, num_ifaces):
        """Create the namespace, the bridge, and attach interfaces all at
        once."""
        result = self.ns_init()
        if result != testutils.SUCCESS:
            return result
        result = self.create_bridge()
        if result != testutils.SUCCESS:
            return result
        testutils.log.info(f"Attaching {num_ifaces} interfaces...")
        return self.attach_interfaces(num_ifaces)


def main(argv):
    """main"""
    # This is a simple test script which creates/deletes a virtual environment

    # Configure logging.
    logging.basicConfig(
        filename="ebpfenv.log",
        format="%(levelname)s:%(message)s",
        level=getattr(logging, logging.INFO),
        filemode="w",
    )
    stderr_log = logging.StreamHandler()
    stderr_log.setFormatter(logging.Formatter("%(levelname)s:%(message)s"))
    logging.getLogger().addHandler(stderr_log)

    bridge = Bridge("12345")
    bridge.create_virtual_env(5)
    bridge.ns_del()


if __name__ == "__main__":
    main(sys.argv)

# Virtual Hub

The idea of virtual-hub is to setup a 802.15.4 network for multiple
QEMU nodes running zephyr application. It receive packets from QEMU
pipes and redirect them to other QEMUs simulating a physical
environment.

This component has a similar idea of running zephyr applications with
`make server` and `make client` which connects both virtual machines
with a simbolic link. Here we try to expand this concept to how many
virtual nodes do you want.

The virtual-hub is a basic cmake application. To build it, follow
the steps:
```
mkdir build && cd build
cmake ..
make
```

To run the virtual-hub you will need to pass an csv file
as an argument:
```
./hub ../input.csv
```

The csv file represents a graph (conection matrix). It enables the
possibility to simulate different topologies between the QEMU nodes.
There is an example file in virtual-hub's directory for 3 nodes full
connected.

When building the matrix you can simulate packet drop rate:
- 0 means that no packets will be delivered
- 1 means that all packets will be delivered
- If you set 0.7 means 70% of packets will be delivered

When building the zephyr application it is necessary to enable the
QEMU pipes management mechanism. To do this we must set QEMU_PIPE_STACK
to 1 on CMakeLists.txt from the target projects.

Finally, We need to setup the pipe id using `QEMU_PIPE_ID` to reference
the pipe which will be used by the QEMU instance. Basically, this
operation mode will create a different linux pipe for each QEMU
based on  `QEMU_PIPE_ID`.

To make this, we can use cmake parameters:
```
cd zephyr-application
mkdir build-1 && cd build-1
cmake -DQEMU_PIPE_ID=1 ..
make
make node
```

The `QEMU_PIPE_ID` defined for each zephyr-application represents a line in
the connection matrix from `input.csv`. This way the first line and column
from the file refers to the node with `QEMU_PIPE_ID` defined with 1. In the
same way the second line and column from the file refers to the node with
`QEMU_PIPE_ID` 2, and so on.

The `make node` command assure the node will setup the pipe correctly.
Remember to run all the QEMU nodes you want to be present in the network
before starting the virtual-hub.

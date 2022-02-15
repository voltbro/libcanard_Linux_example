
# UAVCAN Libcanard 2.0 Examples

[Libcanard](https://github.com/UAVCAN/libcanard) is a compact implementation of the UAVCAN/CAN protocol stack in C99/C11 for high-integrity real-time embedded systems.

[UAVCAN](https://uavcan.org) is an open lightweight data bus standard designed for reliable intravehicular communication in aerospace and robotic applications via CANb bus, Ethernet, and other robust transports.

This repo provides the bare minimum example of Libcanard usage, both messages transmition and receiving. It does not use the advised memory allocator [O1heap](https://github.com/pavel-kirienko/o1heap) for the sake of simplicity. Communication with the CAN bus is done via [SocketCAN](https://www.kernel.org/doc/html/latest/networking/can.html) virtual interface so it can be deployed on almost any machine. Porting this code on the actual hardware (both PC and MCU) is simple - replace the SocketCAN with the driver specific for your setup. 

## Building the examples
This repo contains two examples - one subscribes for the Bit-type message, another one waits for an Array of Real64s. To build any example just enter its directory and run the following commands:
```bash
gcc -Iinclude/ main.c -o main.o -c
gcc ./include/libcanard/canard.c -o canard.o -c
gcc -o demo main.o canard.o
```
## Running the examples
Before launching the example we have to setup virtual CAN interface:
```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```
To see the messages on the bus type: **`candump vcan0`**
Open another terminal window in the project directory and launch our example:  **`./demo`**
The previous window should start print CAN frames:
```bash
vcan0  107D5560   [8]  00 00 00 00 00 00 00 E0
vcan0  107D5560   [8]  01 00 00 00 00 00 00 E1
vcan0  107D5560   [8]  02 00 00 00 00 00 00 E2
...
```
Bare CAN frames do not provide much information. To see the actual UAVCAN messages they are carrying we will use [Yakut](https://github.com/UAVCAN/yakut) tool. [Install](https://github.com/UAVCAN/yakut#installing) it, then open new terminal window in the UAVCAN workspace directory. Before using Yakut we once again have to generate messages definitions, this time we will be using not the Nunavut, but the Yakut itself:
```bash
yakut compile  public_regulated_data_types/uavcan public_regulated_data_types/reg
```
Tell Yakut which interface we are using and then launch subscribtion:
```bash
export UAVCAN__CAN__IFACE=socketcan:vcan0
yakut sub uavcan.node.Heartbeat.1.0
```
Now we can finally see the heartbeat messages in readable form:
```bash
---
7509:
  _metadata_:
    timestamp: {system: 1643369660.008005, monotonic: 660.968789}
    priority: nominal
    transfer_id: 3
    source_node_id: 96
  uptime: 3
  health: {value: 0}
  mode: {value: 0}
  vendor_specific_status_code: 0
```
Our UAVCAN node does not only publish heartbeat messages, but also subscribes for the messages on the bus. One example waits for the Bit-type message, another one waits for an Array of Real64. To publish Bit message run the following commands:
```bash
yakut -i 'CAN(can.media.socketcan.SocketCANMedia("vcan0",8),59)' pub 1620.uavcan.primitive.scalar.Bit.1.0 'value: true'
```
or
```bash
yakut -i 'CAN(can.media.socketcan.SocketCANMedia("vcan0",8),59)' pub 1620.uavcan.primitive.scalar.Bit.1.0 'value: false'
```
To publish Array-Real64 use:
```bash
yakut -i 'CAN(can.media.socketcan.SocketCANMedia("vcan0",8),59)' pub 1620.uavcan.primitive.array.Real64.1.0 'value: [1.1,2.2,3.3,4.4,5.5]'
```

## Building project from scratch
First of all, create the workspace directory:
```bash
mkdir uavcan_ws
cd uavcan_ws
```
Clone [Libcanard](https://github.com/UAVCAN/libcanard) and  [public_regulated_data_types](https://github.com/UAVCAN/public_regulated_data_types) into.
To use standard UAVCAN messages in C we have to generate headers first. To do so we will be using  [Nunavut](https://github.com/UAVCAN/nunavut). [Install](https://github.com/UAVCAN/nunavut#installation) it, then execute following:
```bash
nnvg --target-language c --target-endianness=little --enable-serialization-asserts public_regulated_data_types/reg --lookup-dir public_regulated_data_types/uavcan
nnvg --target-language c --target-endianness=little --enable-serialization-asserts public_regulated_data_types/uavcan --lookup-dir public_regulated_data_types/uavcan
```
Copy the generated sources into the new directory for our demo project:
```bash
mkdir demo/include
cp -R nunavut_out/nunavut/ nunavut_out/reg/ nunavut_out/uavcan/ demo/include/
```
To use freshly generated serialization header we have to add the assert definition:
```bash
chmod +w demo/include/nunavut/support/serialization.h 
nano demo/include/nunavut/support/serialization.h 
```
Find the line 70 - **`#ifndef NUNAVUT_ASSERT`** - and add the **`#define NUNAVUT_ASSERT(x) assert(x)`** line before it.

Now copy the Libcanard sources:
```bash
cp -R libcanard/libcanard/ demo/include/
```
Copy the main.c from this repo desired example into the demo directory. "bit" example creates node what publishes heartbeat messages with 1s period and also subscribes for the Bit-type message. "array" example publishes heartbeat too but listens not for the Bit but Real64 array instead. 
Compile and link the project:
```bash
cd demo
gcc -Iinclude/ main.c -o main.o -c
gcc ./include/libcanard/canard.c -o canard.o -c
gcc -o demo main.o canard.o
```

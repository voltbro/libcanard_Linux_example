// standard C headers
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdint-gcc.h>

// Linux CAN related headers
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

// UAVCAN related headers
#include "libcanard/canard.h"
#include "uavcan/node/Heartbeat_1_0.h"
#include "uavcan/primitive/scalar/Bit_1_0.h"

// timestamp conversion macros
#define SEC_TO_US(sec) ((sec)*1000000)
#define NS_TO_US(ns)    ((ns)/1000)

// Get a systems timestamp in microseconds
uint64_t micros()
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    uint64_t us = SEC_TO_US((uint64_t)ts.tv_sec) + NS_TO_US((uint64_t)ts.tv_nsec);
    return us;
}

// CAN socket instance to communicate with system CAN driver
int s = 0;

CanardInstance 	canard;		// This is the core structure that keeps all of the states and allocated resources of the library instance
CanardTxQueue 	queue;		// Prioritized transmission queue that keeps CAN frames destined for transmission via one CAN interface

// Wrappers for using memory allocator with libcanard
static void *memAllocate(CanardInstance *const canard, const size_t amount);
static void memFree(CanardInstance *const canard, void *const pointer);

// Application-specific function prototypes
void process_canard_TX_queue(void);
void process_canard_receiption(void);

static uint8_t my_message_transfer_id = 0;

// Uptime counter variable for heartbeat message
uint32_t test_uptimeSec = 0;

// buffer for serialization of heartbeat message
size_t hbeat_ser_buf_size = uavcan_node_Heartbeat_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_;
uint8_t hbeat_ser_buf[uavcan_node_Heartbeat_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];

CanardPortID const MSG_PORT_ID   = 1620U;

int main(int argc, char **argv)
{
	printf("Program started...\n");

	// The next block of code setups socketcan interface
	struct sockaddr_can addr;
	if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0)
	{
		perror("Socket");
		exit(0);
	}

	fcntl(s, F_SETFL, O_NONBLOCK);	// non blocking CAN frame receiving => reading from this socket does not block execution

	struct ifreq ifr;
	strcpy(ifr.ifr_name, "vcan0");
	ioctl(s, SIOCGIFINDEX, &ifr);

	memset(&addr, 0, sizeof(addr));
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;
	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		perror("Bind");
		exit(0);
	}

	// UAVCAN initialization
	canard = canardInit(&memAllocate, &memFree);	// Initialization of a canard instance
	canard.node_id = 96;

	queue = canardTxInit(	100,                 		// Limit the size of the queue at 100 frames.
							CANARD_MTU_CAN_CLASSIC); 	// Set MTU = 64 bytes. There is also CANARD_MTU_CAN_CLASSIC.

    CanardRxSubscription subscription; // Transfer subscription state.

	if( canardRxSubscribe(	(CanardInstance *const)&canard,
							CanardTransferKindMessage,
							MSG_PORT_ID,
							uavcan_primitive_scalar_Bit_1_0_EXTENT_BYTES_,
							CANARD_DEFAULT_TRANSFER_ID_TIMEOUT_USEC,
							&subscription) != 1 )
							{
								perror("Subscribe");
								exit(0);
							}							

	while(1)
	{
		// Create a heartbeat message
		uavcan_node_Heartbeat_1_0 test_heartbeat = {
			.uptime = test_uptimeSec,
			.health = {uavcan_node_Health_1_0_NOMINAL},
			.mode = {uavcan_node_Mode_1_0_OPERATIONAL}};

		// Serialize the heartbeat message
		if (uavcan_node_Heartbeat_1_0_serialize_(&test_heartbeat, hbeat_ser_buf, &hbeat_ser_buf_size) < 0)
		{
			perror("Serialize");
			exit(0);
		}

		// Create a transfer for the heartbeat message
		const CanardTransferMetadata transfer_metadata  = {
			.priority = CanardPriorityNominal,
			.transfer_kind = CanardTransferKindMessage,
			.port_id = uavcan_node_Heartbeat_1_0_FIXED_PORT_ID_,
			.remote_node_id = CANARD_NODE_ID_UNSET,
			.transfer_id = my_message_transfer_id,
		};

		if( canardTxPush(	&queue,               	// Call this once per redundant CAN interface (queue)
							&canard,
							0,     					// Zero if transmission deadline is not limited.
							&transfer_metadata,
							hbeat_ser_buf_size,		// Size of the message payload (see Nunavut transpiler)
							hbeat_ser_buf) < 0 )
							{
								perror("TxPush");
								exit(0);
							}

		uint32_t timestamp = time(NULL);

		// Block for a second before generating the next transfer, spin transmission and receiption
		while (time(NULL) < timestamp + 1)
		{
			process_canard_TX_queue();
			process_canard_receiption();
			usleep(50000); // wait for 50ms
		}

		// Increment the transfer_id variable
		my_message_transfer_id++;

		// Increase uptime
		test_uptimeSec++;
	}

	return 0;
}

// allocate dynamic memory of desired size in bytes
static void *memAllocate(CanardInstance *const canard, const size_t amount)
{
	(void)canard;
	return malloc(amount);
}

// free allocated memory
static void memFree(CanardInstance *const canard, void *const pointer)
{
	(void)canard;
	free(pointer);
}

void process_canard_TX_queue(void)
{
	// Look at top of the TX queue of individual CAN frames
	for (const CanardTxQueueItem* ti = NULL; (ti = canardTxPeek(&queue)) != NULL;)
	{
		if ((0U == ti->tx_deadline_usec) || (ti->tx_deadline_usec > micros()))  // Check the deadline.
		{
			// Instantiate a fdframe for the media layer
			struct can_frame txframe;
			txframe.can_dlc = ti->frame.payload_size;
			txframe.can_id = ti->frame.extended_can_id | CAN_EFF_FLAG;

			memcpy(&txframe.data, (uint8_t *)ti->frame.payload, ti->frame.payload_size);

			if (write(s, &txframe, sizeof(struct can_frame)) != sizeof(struct can_frame))
			{
				break;	// If the driver is busy, break.
			}
		}
		// After the frame is transmitted or if it has timed out while waiting, pop it from the queue and deallocate:
		canard.memory_free(&canard, canardTxPop(&queue, ti));
	}
}

void process_canard_receiption(void)
{
	struct can_frame rxframe;

	uint8_t nbytes = read(s, &rxframe, CAN_MTU);
	if( nbytes != CAN_MTU ) // only complete CAN frames are accepted.
	{
		return ;
	}

	CanardFrame rxf;

	uint32_t msg_id = (uint32_t)rxframe.can_id;
	msg_id = msg_id & ~(1 << 31); // clear EFF flag	

	rxf.extended_can_id = msg_id;
	rxf.payload_size = (size_t)rxframe.can_dlc;
	rxf.payload = (void*)&rxframe.data;

	CanardRxTransfer transfer;

	if( canardRxAccept(	(CanardInstance *const)&canard,
						micros(),
						&rxf,
						0,
						&transfer,
						NULL) != 1 )
						{
							return ; // the frame received is not a valid transfer
						}

	uavcan_primitive_scalar_Bit_1_0 bit;		
	size_t bit_ser_buf_size = uavcan_primitive_scalar_Bit_1_0_EXTENT_BYTES_;

	if ( uavcan_primitive_scalar_Bit_1_0_deserialize_( &bit, transfer.payload, &bit_ser_buf_size) < 0)
	{
		perror("Deserialize");
		exit(0);
	}

	canard.memory_free(&canard, transfer.payload);

	printf("Received bit message, value = %d\n", (bool)bit.value);

	return ;
}
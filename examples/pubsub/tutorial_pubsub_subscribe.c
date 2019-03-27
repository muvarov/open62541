/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information.
 */

#define _GNU_SOURCE
/**
 * IMPORTANT ANNOUNCEMENT
 * The PubSub subscriber API is currently not finished. This examples can be used to receive
 * and print the values, which are published by the tutorial_pubsub_publish example.
 * The following code uses internal API which will be later replaced by the higher-level
 * PubSub subscriber API.
*/
#include "ua_pubsub_networkmessage.h"
#include "ua_log_stdout.h"
#include "ua_server.h"
#include "ua_config_default.h"
#include "ua_pubsub.h"
#include "ua_network_pubsub_udp.h"
#ifdef UA_ENABLE_PUBSUB_ETH_UADP
#include "ua_network_pubsub_ethernet.h"
#endif
#include "src_generated/ua_types_generated.h"

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>

#include <sched.h>
#include <pthread.h>

UA_Boolean running = true;
#if 0
static void stopHandler(int sign) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "received ctrl-c");
    running = false;
}
#endif

static UA_ByteString buffer = { 0, 0 };
FILE *outfile;

/* Number of measuments */
#define WP 1024 

/* 0 - kernel ts
 * 1 - begin of callback
 * 2 - end of callback
 */

static uint64_t all_ts[3][WP];
static uint64_t w_all_ts[3][WP];

static int pkt_cnt;

static uint64_t ns_ts(void)
{
	struct timespec ts;
	uint64_t ns;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	ns = (uint64_t)ts.tv_sec * 1000000000LL + (uint64_t)ts.tv_nsec;
	return ns;
}

void tstamp0(uint64_t ts);
void tstamp0_current(void);

void tstamp0(uint64_t ts)
{
	all_ts[0][pkt_cnt] = ts;
}

void tstamp0_current(void)
{
	tstamp0(ns_ts());
}

static void tstamp1(uint64_t ts)
{
	all_ts[1][pkt_cnt] = ts;
}

static void tstamp1_current(void)
{
	tstamp1(ns_ts());
}

static void tstamp2(uint64_t ts)
{
	all_ts[2][pkt_cnt] = ts;
}

static void tstamp2_current(void)
{
	tstamp2(ns_ts());
}

/* This is a bad hack, we just expect packets_shadow to be flushed into a file
 * before updated
 */
static void *write_to_file(void *ptr)
{
	int j;
	cpu_set_t cpuset;
	const pthread_t pid = pthread_self();

	CPU_ZERO(&cpuset);
	CPU_SET(8, &cpuset);

	rewind(outfile);
	pthread_setaffinity_np(pid, sizeof(cpu_set_t), &cpuset);
	/* file format
	 * Number:App Before callback - Kernel ts: App After callback - Kernel ts
	 */
	for (j = 0; j < WP; j++)
		fprintf(outfile, "%d:%ld:%ld\n", j, w_all_ts[1][j] - w_all_ts[0][j], w_all_ts[2][j] - w_all_ts[0][j]);

	fflush(outfile);
	return NULL;
}

static inline void 
subscriptionPollingCallback(UA_Server *server, UA_PubSubConnection *connection) {
    uint64_t kern_ts;

    tstamp1_current();

    connection->channel->receive(connection->channel, &buffer, NULL, 5);

    /* packet size is 31, ts is at the end of user buff */
    kern_ts = *(uint64_t *)((uintptr_t)buffer.data + 31);
    tstamp0(kern_ts);
 
    tstamp2(0);

    /* Decode the message */
    //UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
    //            "Message length: %lu", (unsigned long) buffer.length);
    UA_NetworkMessage networkMessage;
    memset(&networkMessage, 0, sizeof(UA_NetworkMessage));
    size_t currentPosition = 0;
    UA_NetworkMessage_decodeBinary(&buffer, &currentPosition, &networkMessage);

    /* Is this the correct message type? */
    if(networkMessage.networkMessageType != UA_NETWORKMESSAGE_DATASET)
        goto cleanup;

    /* At least one DataSetMessage in the NetworkMessage? */
    if(networkMessage.payloadHeaderEnabled &&
       networkMessage.payloadHeader.dataSetPayloadHeader.count < 1)
        goto cleanup;

    /* Is this a KeyFrame-DataSetMessage? */
    UA_DataSetMessage *dsm = &networkMessage.payload.dataSetPayload.dataSetMessages[0];
    if(!dsm || dsm->header.dataSetMessageType != UA_DATASETMESSAGE_DATAKEYFRAME)
        goto cleanup;

    /* Loop over the fields and print well-known content types */
    for(int i = 0; i < dsm->data.keyFrameData.fieldCount; i++) {
        const UA_DataType *currentType = dsm->data.keyFrameData.dataSetFields[i].value.type;
        if(currentType == &UA_TYPES[UA_TYPES_BYTE]) {
            //UA_Byte value = *(UA_Byte *)dsm->data.keyFrameData.dataSetFields[i].value.data;
            //UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
            //            "Message content: [Byte] \tReceived data: %i", value);
        } else if (currentType == &UA_TYPES[UA_TYPES_DATETIME]) {
#if 0
            UA_DateTime value = *(UA_DateTime *)dsm->data.keyFrameData.dataSetFields[i].value.data;
            UA_DateTimeStruct receivedTime = UA_DateTime_toStruct(value);
            UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_USERLAND,
                        "Message content: [DateTime] \t"
                        "Received date: %02i-%02i-%02i Received time: %02i:%02i:%02i",
                        receivedTime.year, receivedTime.month, receivedTime.day,
                        receivedTime.hour, receivedTime.min, receivedTime.sec);
#endif
	   tstamp2_current();
        }
    }

 cleanup:
    UA_NetworkMessage_clear(&networkMessage);
}


static inline void 
__subscriptionPollingCallback(UA_Server *server, UA_PubSubConnection *connection) {
    pthread_t thread1;

    for (pkt_cnt = 0; pkt_cnt < WP; ) {
	subscriptionPollingCallback(server, connection);
	if (all_ts[2][pkt_cnt] != 0) {
		pkt_cnt++;
	}
    }

    memcpy(w_all_ts, all_ts, sizeof(w_all_ts));
    pthread_create(&thread1, NULL, write_to_file, NULL);
}

static int
run(UA_String *transportProfile, UA_NetworkAddressUrlDataType *networkAddressUrl) {
    //signal(SIGINT, stopHandler);
    //signal(SIGTERM, stopHandler);

    UA_ServerConfig *config = UA_ServerConfig_new_minimal(4801, NULL);
    /* Details about the PubSubTransportLayer can be found inside the
     * tutorial_pubsub_connection */
    config->pubsubTransportLayers = (UA_PubSubTransportLayer *)
        UA_calloc(2, sizeof(UA_PubSubTransportLayer));
    if (!config->pubsubTransportLayers) {
        UA_ServerConfig_delete(config);
        return EXIT_FAILURE;
    }
    config->pubsubTransportLayers[0] = UA_PubSubTransportLayerUDPMP();
    config->pubsubTransportLayersSize++;
#ifdef UA_ENABLE_PUBSUB_ETH_UADP
    config->pubsubTransportLayers[1] = UA_PubSubTransportLayerEthernet();
    config->pubsubTransportLayersSize++;
#endif
    UA_Server *server = UA_Server_new(config);

    UA_PubSubConnectionConfig connectionConfig;
    memset(&connectionConfig, 0, sizeof(connectionConfig));
    connectionConfig.name = UA_STRING("UADP Connection 1");
    connectionConfig.transportProfileUri = *transportProfile;
    connectionConfig.enabled = UA_TRUE;
    UA_Variant_setScalar(&connectionConfig.address, networkAddressUrl,
                         &UA_TYPES[UA_TYPES_NETWORKADDRESSURLDATATYPE]);
    UA_NodeId connectionIdent;
    UA_StatusCode retval =
        UA_Server_addPubSubConnection(server, &connectionConfig, &connectionIdent);
    if(retval == UA_STATUSCODE_GOOD)
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER,
                    "The PubSub Connection was created successfully!");

    /* The following lines register the listening on the configured multicast
     * address and configure a repeated job, which is used to handle received
     * messages. */
    UA_PubSubConnection *connection =
        UA_PubSubConnection_findConnectionbyId(server, connectionIdent);
#if 0
    if(connection != NULL) {
        UA_StatusCode rv = connection->channel->regist(connection->channel, NULL, NULL);
        if (rv == UA_STATUSCODE_GOOD) {
            UA_UInt64 subscriptionCallbackId;
            UA_Server_addRepeatedCallback(server, (UA_ServerCallback)subscriptionPollingCallback,
                                          connection, 100, &subscriptionCallbackId);
        } else {
            UA_LOG_WARNING(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "register channel failed: %s!",
                           UA_StatusCode_name(rv));
        }
    }
#else

    if ((outfile = fopen("opc_ua_speed.log", "w+")) == NULL) {
	printf("unable to open opc_ua_speed.log\n");
	exit(-1);
    }

    if(connection != NULL) {
        connection->channel->regist(connection->channel, NULL, NULL);
    	UA_Server_run_startup(server);
	UA_ByteString_allocBuffer(&buffer, 512);

	subscriptionPollingCallback(server, connection);
	while (1) {
		__subscriptionPollingCallback(server, connection);
	}
   } 
#endif

    retval |= UA_Server_run(server, &running);
    UA_Server_delete(server);
    UA_ServerConfig_delete(config);
    return retval == UA_STATUSCODE_GOOD ? EXIT_SUCCESS : EXIT_FAILURE;;
}


static void
usage(char *progname) {
    printf("usage: %s <uri> [device]\n", progname);
}

int main(int argc, char **argv) {
    UA_String transportProfile =
        UA_STRING("http://opcfoundation.org/UA-Profile/Transport/pubsub-udp-uadp");
    UA_NetworkAddressUrlDataType networkAddressUrl =
        {UA_STRING_NULL , UA_STRING("opc.udp://224.0.0.22:4840/")};

    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (strncmp(argv[1], "opc.udp://", 10) == 0) {
            networkAddressUrl.url = UA_STRING(argv[1]);
        } else if (strncmp(argv[1], "opc.eth://", 10) == 0) {
            transportProfile =
                UA_STRING("http://opcfoundation.org/UA-Profile/Transport/pubsub-eth-uadp");
            if (argc < 3) {
                printf("Error: UADP/ETH needs an interface name\n");
                return EXIT_FAILURE;
            }
            networkAddressUrl.networkInterface = UA_STRING(argv[2]);
            networkAddressUrl.url = UA_STRING(argv[1]);
        } else {
            printf("Error: unknown URI\n");
            return EXIT_FAILURE;
        }
    }

    return run(&transportProfile, &networkAddressUrl);
}

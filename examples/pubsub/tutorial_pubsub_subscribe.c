/* This work is licensed under a Creative Commons CCZero 1.0 Universal License.
 * See http://creativecommons.org/publicdomain/zero/1.0/ for more information.
 */

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

UA_Boolean running = true;
#if 0
static void stopHandler(int sign) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "received ctrl-c");
    running = false;
}
#endif

static UA_ByteString buffer = { 0, 0 };

static inline uint64_t ns_ts(void)
{
       return __builtin_ia32_rdtsc();
}


const int NANO_SECONDS_IN_SEC = 1000000000;
/* returns a static buffer of struct timespec with the time difference of ts1 and ts2
   ts1 is assumed to be greater than ts2 */
static inline struct timespec *TimeSpecDiff(struct timespec *ts1, struct timespec *ts2)
{
  static struct timespec ts;
  ts.tv_sec = ts1->tv_sec - ts2->tv_sec;
  ts.tv_nsec = ts1->tv_nsec - ts2->tv_nsec;
  if (ts.tv_nsec < 0) {
    ts.tv_sec--;
    ts.tv_nsec += NANO_SECONDS_IN_SEC;
  }
  return &ts;
}

static double g_TicksPerNanoSec;

static void CalibrateTicks(void)
{
  struct timespec begints, endts;
  uint64_t begin = 0, end = 0;
  clock_gettime(CLOCK_MONOTONIC, &begints);
  begin = ns_ts();
  uint64_t i;
  for (i = 0; i < 1000000; i++); /* must be CPU intensive */
  end = ns_ts();
  clock_gettime(CLOCK_MONOTONIC, &endts);
  struct timespec *tmpts = TimeSpecDiff(&endts, &begints);
  uint64_t nsecElapsed = (uint64_t)tmpts->tv_sec * 1000000000LL + (uint64_t)tmpts->tv_nsec;
  g_TicksPerNanoSec = (double)(end - begin)/(double)nsecElapsed;
  printf("Ticks per socond %f\n", g_TicksPerNanoSec);
}

#define NUMBER_OF_EXECS 10000000

static inline void 
subscriptionPollingCallback(UA_Server *server, UA_PubSubConnection *connection) {
    //UA_StatusCode retval;
#if 0
    uint64_t ts;
    uint64_t prev_ts;
    uint64_t max_ts = 0;
 
    prev_ts = ns_ts();
    for (uint64_t i = 0; i < NUMBER_OF_EXECS; i++) {
	ts = ns_ts();
	if ((ts - prev_ts) > max_ts)
		max_ts = ts - prev_ts;
	prev_ts = ts;
    	connection->channel->receive(connection->channel, &buffer, NULL, 5);
   }

   //return max_ts;
#endif
#if 1
    	connection->channel->receive(connection->channel, &buffer, NULL, 5);
   	//if(retval != UA_STATUSCODE_GOOD || buffer.length == 0) {
        //return;
	//}
  

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
        }
    }

 cleanup:
    UA_NetworkMessage_clear(&networkMessage);
#endif
}


static inline void 
__subscriptionPollingCallback(UA_Server *server, UA_PubSubConnection *connection) {
    for (uint64_t i = 0; i < NUMBER_OF_EXECS; i++)
	subscriptionPollingCallback(server, connection);

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



    if(connection != NULL) {
        connection->channel->regist(connection->channel, NULL, NULL);
    	UA_Server_run_startup(server);
	UA_ByteString_allocBuffer(&buffer, 512);

	subscriptionPollingCallback(server, connection);
        CalibrateTicks();
	while (1) {
		uint64_t end_ts;
		uint64_t start_ts = ns_ts();
		__subscriptionPollingCallback(server, connection);
		end_ts = ns_ts();
        	printf("Max ts diff is %ld ns, %ld us\n", (end_ts - start_ts)/NUMBER_OF_EXECS/(unsigned int)g_TicksPerNanoSec, (end_ts - start_ts)/ NUMBER_OF_EXECS / 1000 / (unsigned int)g_TicksPerNanoSec);
		 
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

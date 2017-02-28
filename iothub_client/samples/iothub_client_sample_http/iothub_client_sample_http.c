// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdio.h>
#include <stdlib.h>

/* This sample uses the _LL APIs of iothub_client for example purposes.
That does not mean that HTTP only works with the _LL APIs.
Simply changing the using the convenience layer (functions not having _LL)
and removing calls to _DoWork will yield the same results. */

#ifdef ARDUINO
#include "AzureIoT.h"
#else
#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "azure_c_shared_utility/platform.h"
#include "iothub_client_ll.h"
#include "iothub_message.h"
#include "iothubtransporthttp.h"
#endif

#ifdef MBED_BUILD_TIMESTAMP
#include "certs.h"
#endif // MBED_BUILD_TIMESTAMP

/*String containing Hostname, Device Id & Device Key in the format:                         */
/*  "HostName=<host_name>;DeviceId=<device_id>;SharedAccessKey=<device_key>"                */
/*  "HostName=<host_name>;DeviceId=<device_id>;SharedAccessSignature=<device_sas_token>"    */
static const char* connectionString =
        "HostName=bertk-dt-hub.azure-devices.net;DeviceId=test;SharedAccessSignature=SharedAccessSignature sr=bertk-dt-hub.azure-devices.net%2Fdevices%2Ftest&sig=aJ9nobs%2FxO%2FA%2B%2ByG8fbeTmLUrjacZLhIboCtud0Rutc%3D&se=1496181936";



static int callbackCounter;
static bool g_continueRunning;
static char msgText[512];
static char propText[512];
#define MESSAGE_COUNT       5
#define DOWORK_LOOP_NUM     3

#ifdef NO_VERBOSE_OUTPUT
#define verbose_printf(...) 
#else
#define verbose_printf(...) printf(__VA_ARGS__)
#endif


typedef struct EVENT_INSTANCE_TAG
{
    IOTHUB_MESSAGE_HANDLE messageHandle;
    size_t messageTrackingId;  // For tracking the messages within the user callback.
} EVENT_INSTANCE;

static IOTHUBMESSAGE_DISPOSITION_RESULT ReceiveMessageCallback(IOTHUB_MESSAGE_HANDLE message, void* userContextCallback)
{
    int* counter = (int*)userContextCallback;
    const char* buffer;
    size_t size;
    MAP_HANDLE mapProperties;

    if (IoTHubMessage_GetByteArray(message, (const unsigned char**)&buffer, &size) != IOTHUB_MESSAGE_OK)
    {
        LogError("unable to retrieve the message data");
    }
    else
    {
        verbose_printf("Received Message [%d] with Data: <<<%.*s>>> & Size=%d\r\n", *counter, (int)size, buffer, (int)size);
        if (size == (strlen("quit") * sizeof(char)) && memcmp(buffer, "quit", size) == 0)
        {
            g_continueRunning = false;
        }
    }

    // Retrieve properties from the message
    mapProperties = IoTHubMessage_Properties(message);
    if (mapProperties != NULL)
    {
        const char*const* keys;
        const char*const* values;
        size_t propertyCount = 0;
        if (Map_GetInternals(mapProperties, &keys, &values, &propertyCount) == MAP_OK)
        {
            if (propertyCount > 0)
            {
                size_t index;

                verbose_printf("Message Properties:\r\n");
                for (index = 0; index < propertyCount; index++)
                {
                    verbose_printf("\tKey: %s Value: %s\r\n", keys[index], values[index]);
                }
                verbose_printf("\r\n");
            }
        }
    }

    /* Some device specific action code goes here... */
    (*counter)++;
    return IOTHUBMESSAGE_ACCEPTED;
}

static void SendConfirmationCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* userContextCallback)
{
    EVENT_INSTANCE* eventInstance = (EVENT_INSTANCE*)userContextCallback;
    
    printf("C[%d] id = %d r = %d\r\n", callbackCounter, eventInstance->messageTrackingId, result);
    
    /* Some device specific action code goes here... */
    callbackCounter++;
    IoTHubMessage_Destroy(eventInstance->messageHandle);
}

void iothub_client_sample_http_run(void)
{
    IOTHUB_CLIENT_LL_HANDLE iotHubClientHandle;

    EVENT_INSTANCE messages[MESSAGE_COUNT];
    double avgWindSpeed = 10.0;
    int receiveContext = 0;

    g_continueRunning = true;

    srand((unsigned int)time(NULL));

    callbackCounter = 0;

    if (platform_init() != 0)
    {
        LogError("Failed to initialize the platform.");
    }
    else
    {
        verbose_printf("Starting the IoTHub client sample HTTP...\r\n");

        if ((iotHubClientHandle = IoTHubClient_LL_CreateFromConnectionString(connectionString, HTTP_Protocol)) == NULL)
        {
            LogError("ERROR: iotHubClientHandle is NULL!");
        }
        else
        {
            
            // Because it can poll "after 9 seconds" polls will happen effectively // at ~10 seconds.
            // Note that for scalabilty, the default value of minimumPollingTime
            // is 25 minutes. For more information, see:
            // https://azure.microsoft.com/documentation/articles/iot-hub-devguide/#messaging
            unsigned int minimumPollingTime = 9;
            /*
            uint32_t timeout = 241000;
            if (IoTHubClient_LL_SetOption(iotHubClientHandle, "timeout", &timeout) != IOTHUB_CLIENT_OK)
            {
                LogError("failure to set option \"timeout\"");
            }
            */

            if (IoTHubClient_LL_SetOption(iotHubClientHandle, "MinimumPollingTime", &minimumPollingTime) != IOTHUB_CLIENT_OK)
            {
                LogError("failure to set option \"MinimumPollingTime\"");
            }

#ifdef MBED_BUILD_TIMESTAMP
            // For mbed add the certificate information
            if (IoTHubClient_LL_SetOption(iotHubClientHandle, "TrustedCerts", certificates) != IOTHUB_CLIENT_OK)
            {
                LogError("failure to set option \"TrustedCerts\"");
            }
#endif // MBED_BUILD_TIMESTAMP

            /* Setting Message call back, so we can receive Commands. */
            if (IoTHubClient_LL_SetMessageCallback(iotHubClientHandle, ReceiveMessageCallback, &receiveContext) != IOTHUB_CLIENT_OK)
            {
                LogError("ERROR: IoTHubClient_LL_SetMessageCallback..........FAILED!");
            }
            else
            {
                verbose_printf("IoTHubClient_LL_SetMessageCallback...successful.");

                /* Now that we are ready to receive commands, let's send some messages */
                size_t iterator = 0;
                do
                {
                    if (iterator < MESSAGE_COUNT)
                    {
                        sprintf_s(msgText, sizeof(msgText), "{\"deviceId\": \"myFirstDevice\",\"windSpeed\": %.2f}", avgWindSpeed + (rand() % 4 + 2));
                        if ((messages[iterator].messageHandle = IoTHubMessage_CreateFromByteArray((const unsigned char*)msgText, strlen(msgText))) == NULL)
                        {
                            LogError("ERROR: iotHubMessageHandle is NULL!");
                        }
                        else
                        {
                            MAP_HANDLE propMap;

                            messages[iterator].messageTrackingId = iterator;

                            propMap = IoTHubMessage_Properties(messages[iterator].messageHandle);
                            (void)sprintf_s(propText, sizeof(propText), "PropMsg_%zu", iterator);
                            if (Map_AddOrUpdate(propMap, "PropName", propText) != MAP_OK)
                            {
                                LogError("ERROR: Map_AddOrUpdate Failed!");
                            }

                            if (IoTHubClient_LL_SendEventAsync(iotHubClientHandle, messages[iterator].messageHandle, SendConfirmationCallback, &messages[iterator]) != IOTHUB_CLIENT_OK)
                            {
                                LogError("ERROR: IoTHubClient_LL_SendEventAsync..........FAILED!");
                            }
                            else
                            {
                                verbose_printf("IoTHubClient_LL_SendEventAsync accepted message [%zu] for transmission to IoT Hub.\r\n", iterator);
                            
                            }

                        }
                    }
                    IoTHubClient_LL_DoWork(iotHubClientHandle);
                    ThreadAPI_Sleep(1);

                    iterator++;
                } while (g_continueRunning);
                
                verbose_printf("iothub_client_sample_http has gotten quit message, call DoWork %d more time to complete final sending...\r\n", DOWORK_LOOP_NUM);
                for (size_t index = 0; index < DOWORK_LOOP_NUM; index++)
                {
                    IoTHubClient_LL_DoWork(iotHubClientHandle);
                    ThreadAPI_Sleep(1);
                }
            }
            IoTHubClient_LL_Destroy(iotHubClientHandle);
        }
        platform_deinit();
    }
}

int main(void)
{
   iothub_client_sample_http_run();
   return 0;
}

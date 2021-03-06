#include <stdio.h>
#include "logger.h"
#ifndef __XTENSA__
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#endif
#include <unistd.h>
#include <time.h>
#include "app.h"
#include "jwt.h"
#include "msg_mqtt.h"
#include "msg_gcp_mqtt.h"
#include "msg_pipe.h"
#include "msg_tcpip.h"
#include "msg_utils.h"

#include "phev_core.h"
#include "phev_controller.h"
#include "tcp_client.h"
#include "wifi_client.h"

#define FILENAME "main/resources/config-local.json"

#define MAX_CLIENT_ID_SIZE 255

const static char *APP_TAG = "PHEV_APP";

void outgoingHandler(messagingClient_t * client, message_t * message)
{
    printf("New Message\n");
}
message_t * incomingHandler(messagingClient_t *client) 
{
    return NULL;
}
int dummy_connect(messagingClient_t * client)
{
    client->connected = true;
    return 0;
}

char * app_displayConfig(phevConfig_t * config)
{
    const char * FORMAT = "Config\nCar Connection\n\tHost %s\n\tPort %d\n\tSSID %s\n\tPassword %s\n";
    char * out = NULL;
    asprintf(&out,FORMAT,config->connectionConfig.host, config->connectionConfig.port,
            config->connectionConfig.carConnectionWifi.ssid, config->connectionConfig.carConnectionWifi.password);
    return out;
}
messagingClient_t * setupAppMsgClient(void)
{
    messagingSettings_t settings = {
        .connect = dummy_connect,
        .incomingHandler = incomingHandler,
        .outgoingHandler = outgoingHandler,
    };

    messagingClient_t * client = msg_core_createMessagingClient(settings);
    return client;
}
int connectToCar(const char *host, uint16_t port)
{
    LOG_D(APP_TAG,"Connect to car - Host %s Port %d",host,port);

    return tcp_client_connectSocket(host,port);
}
phevCtx_t * app_createPhevController(msg_mqtt_t mqtt, phevStore_t * store)
{
    LOG_V(APP_TAG,"START - createPhevController");
    gcpSettings_t inSettings = {
        .uri = "mqtts://mqtt.googleapis.com:8883",
        .device = store->deviceId,
        .createJwt = createJwt,
        .mqtt = &mqtt,
        .projectId = store->config->gcpProjectId,
    }; 
    
    asprintf(&inSettings.clientId,"projects/%s/locations/%s/registries/%s/devices/%s"
        ,store->config->gcpProjectId
        ,store->config->gcpLocation
        ,store->config->gcpRegistry
        ,store->deviceId);
    LOG_D(APP_TAG,"Client ID %s",inSettings.clientId);
    
    inSettings.eventTopic = strdup(store->config->eventsTopic);
    inSettings.stateTopic = strdup(store->config->stateTopic);
    inSettings.commandsTopic = strdup(store->config->commandsTopic);
    inSettings.configTopic = strdup(store->config->configTopic);

    LOG_D(APP_TAG,"Events topic %s",inSettings.eventTopic);
    LOG_D(APP_TAG,"State topic %s",inSettings.stateTopic);
    LOG_D(APP_TAG,"Commands topic %s",inSettings.commandsTopic);
    LOG_D(APP_TAG,"Config topic %s",inSettings.configTopic);
    
    tcpIpSettings_t outSettings = {
        .connect = connectToCar, 
        .read = tcp_client_read,
        .write = tcp_client_write,
    };
    
     
    phevSettings_t phev_settings = {
#if defined(__linux__)
        .in = setupAppMsgClient(),
#else 
        .in = msg_gcp_createGcpClient(inSettings),
#endif
        .out = msg_tcpip_createTcpIpClient(outSettings),
        .startWifi = NULL, //wifi_conn_init,
        .store = store,
    };

    LOG_V(APP_TAG,"END - createPhevController");
    LOG_D(APP_TAG,"Init");
    
    return phev_controller_init(&phev_settings);
}
#if defined(__linux__)
int main()
{

    msg_mqtt_t mqtt = {};
    
    LOG_I(APP_TAG,"System is Linux...");
    LOG_I(APP_TAG,"Loading config from file...");
    FILE * configFile = fopen(FILENAME,"r");
    if (configFile == NULL) {
        printf("Cannot open file");
        exit(-1);
    }
    
    struct stat st;
    stat(FILENAME, &st);
    size_t size = st.st_size;
    
    char * buffer = malloc(size);

    size_t num = fread(buffer,1,size,configFile);
    
    LOG_I(APP_TAG,"Config Loaded...");

    phevCtx_t * ctx = app_createPhevController(mqtt);

    phev_controller_setConfigJson(ctx, buffer);

    LOG_I(APP_TAG,"Config set...");

    char * configTxt = app_displayConfig(ctx->config); 
    
    LOG_I(APP_TAG,"Build version :%d", ctx->config->updateConfig.currentBuild);
    LOG_I(APP_TAG,"Latest build version :%d", ctx->config->updateConfig.latestBuild);
    LOG_I(APP_TAG,"%s",configTxt);

    phev_controller_updateConfig(ctx, ctx->config);
    
    LOG_I(APP_TAG,"Starting message loop...");

    while(1) 
    {
        phev_controller_eventLoop(ctx);
        //sleep(2);
    }    
}
#endif
#ifndef _PHEV_CONFIG_H_
#define _PHEV_CONFIG_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define UPDATE_CONFIG_JSON "update"
#define UPDATE_CONFIG_NAME "update"
#define UPDATE_CONFIG_SSID "ssid"
#define UPDATE_CONFIG_PASSWORD "password"
#define UPDATE_CONFIG_HOST "host"
#define UPDATE_CONFIG_PATH "path"
#define UPDATE_CONFIG_PORT "port"
#define UPDATE_CONFIG_LATEST_BUILD "latestBuild"
#define UPDATE_CONFIG_OVER_GSM "overGsm"
#define UPDATE_CONFIG_FORCE_UPDATE "forceUpdate"

typedef struct phevWifi_t
{
    char ssid[32];
    char password[64];
} phevWifi_t;

typedef struct phevState_t {
    bool headLightsOn;
    bool parkLightsOn;
    bool airConOn;
} phevState_t;

typedef struct phevConnectionConfig_t {
    char * host;
    uint16_t port;
} phevConnectionConfig_t;

typedef struct phevUpdateConfig_t {
    uint32_t * latestBuild;
    int updateOverPPP;
    char * updateUri;
    phevWifi_t updateWifi;
    char * updatePath;
    char * updateHost;
    uint16_t updatePort;
    char * updateImageFullPath;
} phevUpdateConfig_t;

typedef struct phevConfig_t
{
    phevConnectionConfig_t connectionConfig;
    phevUpdateConfig_t updateConfig;
    phevWifi_t carConnectionWifi;
    phevState_t state;
} phevConfig_t;

phevConfig_t * phev_config_parseConfig(const char * config);

#endif
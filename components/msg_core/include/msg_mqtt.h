#ifndef MSG_MQTT_H_
#define MSG_MQTT_H_

#include <stdbool.h>

#include "msg_core.h"

typedef void * mqtt_client_handle_t;
typedef char * topic_t;

typedef enum {
    MSG_MQTT_EVENT_ERROR = 0,
    MSG_MQTT_EVENT_CONNECTED,
    MSG_MQTT_EVENT_DISCONNECTED,
    MSG_MQTT_EVENT_SUBSCRIBED,
    MSG_MQTT_EVENT_UNSUBSCRIBED,
    MSG_MQTT_EVENT_PUBLISHED,
    MSG_MQTT_EVENT_DATA,
} mqtt_event_id_t;

typedef struct {
    mqtt_event_id_t event_id;
    mqtt_client_handle_t client;
    void *user_context;
    char *data;
    int data_len;
    int total_data_len;
    int current_data_offset;
    char *topic;
    int topic_len;
    int msg_id;
} mqtt_event_t;
#define OK 0
typedef uint16_t err_t;
typedef void *handle_t;

typedef enum {
    MSG_MQTT_TRANSPORT_UNKNOWN = 0x0,
    MSG_MQTT_TRANSPORT_OVER_TCP,
    MSG_MQTT_TRANSPORT_OVER_SSL,
    MSG_MQTT_TRANSPORT_OVER_WS,
    MSG_MQTT_TRANSPORT_OVER_WSS
} mqtt_transport_t;

typedef mqtt_event_t * mqtt_event_handle_t;
typedef struct config_t 
{
    handle_t event_handle;
    const char * host;
    const char *uri;
    uint16_t port;
    const char * client_id;
    const char * username;
    const char *password;
    const char *lwt_topic;
    const char *lwt_msg;
    int lwt_qos;
    int lwt_retain;
    int lwt_msg_len;
    int disable_clean_session;
    int keepalive;
    bool disable_auto_reconnect;
    void *user_context;
    int task_prio;
    int task_stack;
    int buffer_size;
    const char *cert_pem;
    mqtt_transport_t transport;
} config_t;

typedef struct msg_mqtt_t
{
    messagingClient_t * client;
    handle_t handle;
    void (* incoming_cb)(messagingClient_t *, message_t *);
    handle_t (*init)(const config_t *config);
    err_t (*set_uri)(handle_t client, const char *uri);
    err_t (*start)(handle_t client);
    err_t (*stop)(handle_t client);
    err_t (*subscribe)(handle_t client, const char *topic, int qos);
    err_t (*unsubscribe)(handle_t client, const char *topic);
    int (*publish)(handle_t client, const char *topic, const char *data, int len, int qos, int retain);
    err_t (*destroy)(handle_t client);
} msg_mqtt_t;

typedef struct msg_mqtt_settings_t
{
    messagingClient_t * client;
    char *host;
    char *clientId;
    uint16_t port;
    char *username;
    char *password;
    msg_mqtt_t * mqtt;

} msg_mqtt_settings_t;

handle_t mqtt_start(msg_mqtt_settings_t * settings);

int publish(msg_mqtt_t *mqtt, topic_t topic, message_t * message);

#endif
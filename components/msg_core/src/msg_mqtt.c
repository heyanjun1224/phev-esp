#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "msg_core.h"
#include "msg_utils.h"
#include "msg_mqtt.h"

void dataEvent(mqtt_event_handle_t event)
{
    char *topic = malloc(event->topic_len + 1);
    uint8_t *data = malloc(event->data_len + 1);
    
    memcpy(data, event->data, event->data_len);
    data[event->data_len] = 0;
    
    
    message_t * message = malloc(sizeof(message_t));

    message->data = data;
    message->length = event->data_len;
    
    ((msg_mqtt_t *) event->user_context)->incoming_cb(((msg_mqtt_t *) event->user_context)->client, message);

}

static msg_mqtt_err_t mqtt_event_handler(mqtt_event_handle_t event)
{
    msg_mqtt_t * mqtt = (msg_mqtt_t *) event->user_context;
    switch (event->event_id) {
        case MSG_MQTT_EVENT_CONNECTED:
            if(mqtt->connected_cb) mqtt->connected_cb(event);
            break;
        case MSG_MQTT_EVENT_DISCONNECTED:
            if(mqtt->disconnected_cb) mqtt->disconnected_cb(event);
            break;
        case MSG_MQTT_EVENT_SUBSCRIBED:
            if(mqtt->subscribed_cb) mqtt->subscribed_cb(event);
            break;
        case MSG_MQTT_EVENT_UNSUBSCRIBED:
            break;
        case MSG_MQTT_EVENT_PUBLISHED:
            if(mqtt->published_cb) mqtt->published_cb(event);
            break;
        case MSG_MQTT_EVENT_DATA:
            if(mqtt->incoming_cb) dataEvent(event);
            break;
        case MSG_MQTT_EVENT_ERROR:
            if(mqtt->error_cb) mqtt->error_cb(event);
            break;
    }
    
    return OK;
}

int publish(msg_mqtt_t * mqtt, topic_t topic, message_t *message)
{
    message_t *msg = msg_utils_copyMsg(message);
    return mqtt->publish((handle_t *) mqtt->handle, topic, (const char *) msg->data, msg->length, 0, 0);
}

void subscribe(msg_mqtt_t * mqtt, topic_t topic)
{
    mqtt->subscribe((handle_t *) mqtt->handle, topic, 0);
}
handle_t mqtt_start(msg_mqtt_settings_t * settings)
{

    msg_mqtt_t * mqtt = settings->mqtt;

    const config_t mqtt_cfg = {
        .event_handle = mqtt_event_handler,
        .user_context = (void *) mqtt,
        .host = settings->host,
        .port = settings->port,
        .client_id = settings->clientId,
        .username = settings->username,
        .password = settings->password,
        .transport = settings->transport,
    };
    printf("JWT %s\n",mqtt_cfg.password);
    handle_t client = mqtt->init(&mqtt_cfg);
    mqtt->start(client);
    mqtt->handle = client;
    mqtt->incoming_cb = settings->incoming_cb;
    mqtt->connected_cb = settings->connected_cb;
    mqtt->disconnected_cb = settings->disconnected_cb;
    mqtt->subscribed_cb = settings->subscribed_cb;
    mqtt->error_cb = settings->error_cb;
    mqtt->published_cb = settings->published_cb;
    mqtt->client = settings->client;
    
    return client;
}
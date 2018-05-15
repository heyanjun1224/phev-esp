#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "msg_core.h"

int msg_core_start(messagingClient_t *client)
{
    return 0;
}
int msg_core_stop(messagingClient_t *client)
{
    return 0;
}
int msg_core_connect(messagingClient_t *client)
{
    return client->connect(client);
}
int msg_core_publish(messagingClient_t *client, message_t *message)
{
    client->outgoingHandler(client, message);
    return 0;
}
int msg_core_registerHandlers(messagingClient_t *client, messagingClientHandler_t incoming, messagingClientHandler_t outgoing) 
{
    client->incomingHandler = incoming;
    client->outgoingHandler = outgoing;
    return 0;
}

void msg_core_call_subs(messagingClient_t *client, message_t *message)
{
    if(message && client->numSubs)
    {
        for(int i = 0;i < client->numSubs;i++)
        {
            if(client->subs[i][0]) 
            {
                client->subs[i][0](client, client->subs[i][1],message);
            }
        }
    }
}
void msg_core_loop(messagingClient_t *client) 
{
    message_t *message = client->incomingHandler(client);

    msg_core_call_subs(client, message);

}
void msg_core_subscribe(messagingClient_t *client, void * params, messagingSubscriptionCallback_t callback)
{
    if(client->numSubs < MAX_SUBSCRIPTIONS) 
    {
        client->subs[client->numSubs][0] = callback;
        client->subs[client->numSubs++][1] = params;
    }
}
int msg_core_messagingClientInit(messagingClient_t **client)
{
    (*client) = (messagingClient_t *) malloc(sizeof(messagingClient_t));
    (*client)->start = msg_core_start;
    (*client)->stop = msg_core_stop;
    (*client)->connect = msg_core_connect;
    (*client)->loop = msg_core_loop;
    (*client)->subscribe = msg_core_subscribe;
    (*client)->publish = msg_core_publish;
    (*client)->numSubs = 0;
    return 0;
}

message_t * msg_core_copyMessage(message_t * message)
{
    message_t * out = malloc(sizeof(message_t));
    
    out->data = malloc(message->length);
    out->length = message->length;
    
    memcpy(out->data,message->data,message->length);
    return out;
}
messagingClient_t * msg_core_createMessagingClient(messagingSettings_t settings)
{
    messagingClient_t * client;

    msg_core_messagingClientInit(&client);
    msg_core_registerHandlers(client, settings.incomingHandler,settings.outgoingHandler);

    client->start = settings.start;
    client->stop = settings.stop;
    client->connect = settings.connect;
    client->connected = 0;
    client->ctx = settings.ctx;

    return client;
}
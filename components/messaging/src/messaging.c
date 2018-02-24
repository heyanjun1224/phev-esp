#include "stdlib.h"
#include "messaging.h"

int start(void)
{
    return 0;
}
int stop(void)
{
    return 0;
}
int publish(messagingClient_t *client, message_t message)
{
    client->outgoingHandler(message);
    return 0;
}
int registerHandlers(messagingClient_t *client, messagingClientHandler_t incoming, messagingClientHandler_t outgoing) 
{
    client->incomingHandler = incoming;
    client->outgoingHandler = outgoing;
    client->publish = publish;
    return 0;
}
int messagingClientInit(messagingClient_t **client)
{
    (*client) = (messagingClient_t *) malloc(sizeof(messagingClient_t));
    (*client)->start = start;
    (*client)->stop = stop;

    return 0;
}

messagingClient_t * createMessagingClient(messagingSettings_t settings)
{
    messagingClient_t * client;

    messagingClientInit(&client);
    registerHandlers(client, settings.incomingHandler,settings.outgoingHandler);

    client->start = settings.start;
    client->stop = settings.stop;
    client->connect = settings.connect;

    return client;
}
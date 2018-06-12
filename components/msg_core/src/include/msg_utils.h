#ifndef _MSG_UTILS_H_
#define _MSG_UTILS_H_

#include <stdint.h>
#include "msg_core.h"

static message_t * msg_utils_createMsg(uint8_t * data, size_t length)
{
    message_t * message = malloc(sizeof(message_t));

    message->data = malloc(length);
    memcpy(message->data, data, length);
    message->length = length;

    return message;
}

static void msg_utils_destroyMsg(message_t * message)
{
    if(message != NULL)
    {
        if(message->data != NULL)
        {
            free(message->data);
        }
        free(message);
    }
}
static message_t * msg_utils_copyMsg(message_t * message)
{
    message_t * out = malloc(sizeof(message_t));
    
    out->data = malloc(message->length);
    out->length = message->length;
    
    memcpy(out->data,message->data,message->length);
    return out;
}

message_t * msg_utils_concatMessages(message_t * message1, message_t * message2);

#endif
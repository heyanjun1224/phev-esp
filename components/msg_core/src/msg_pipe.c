#include <stdio.h>
#include <string.h>
#include "msg_pipe.h"
#include "msg_pipe_splitter.h"
#include "msg_utils.h"
#include "logger.h"

const static char *APP_TAG = "MSG_PIPE";

void msg_pipe_loop(msg_pipe_ctx_t * ctx)
{
    LOG_V(APP_TAG,"START - loop");
    
    ctx->in->loop(ctx->in);
    ctx->out->loop(ctx->out);

    LOG_V(APP_TAG,"END - loop");
    
}

message_t * msg_pipe_transformChain(msg_pipe_ctx_t * ctx, messagingClient_t * client, msg_pipe_chain_t * chain, message_t * message) 
{
    LOG_V(APP_TAG,"START - transformChain");
    
    message_t * msg = message;

    if(chain->inputTransformer != NULL) 
    {
        msg = chain->inputTransformer(ctx->user_context, message);
    }
    msg_utils_destroyMsg(message);
    if(chain->filter != NULL)
    {
        if(!chain->filter(ctx->user_context, msg))
        {
            return NULL;
        }
    }
    if(chain->responder != NULL)
    {
        message_t * response = chain->responder(ctx->user_context, msg);
        if(response != NULL)
        {
            client->publish(client,response);
            LOG_D(APP_TAG,"Destroy message after publish");
            msg_utils_destroyMsg(response);
        }
    }
    message_t * out = NULL;

    if(chain->outputTransformer != NULL)
    {
        out = chain->outputTransformer(ctx->user_context, msg);
    }
    msg_utils_destroyMsg(msg);

    LOG_V(APP_TAG,"END - transformChain");
    
    return out;
}
message_t * msg_pipe_callTransformers(msg_pipe_ctx_t *ctx, messagingClient_t * client, msg_pipe_chain_t * chain, message_t *message)
{
    LOG_V(APP_TAG,"START - callTransformers");
    
    if(chain->splitter != NULL)
    {
        messageBundle_t * messages = msg_pipe_splitter(ctx->user_context, chain, message);
        
        LOG_D(APP_TAG,"Destroy message after splitter");
            
        msg_utils_destroyMsg(message);

        if(messages == NULL) return NULL;

        messageBundle_t * out = malloc(sizeof(messageBundle_t));
        
        out->numMessages = 0;
        
        for(int i=0;i<messages->numMessages;i++) 
        {
            message_t * transMsg = msg_pipe_transformChain(ctx, client, chain, messages->messages[i]);
            if(transMsg != NULL) 
            {
                LOG_D(APP_TAG,"Message %d", out->numMessages);
                LOG_BUFFER_HEXDUMP(APP_TAG,transMsg->data,transMsg->length,LOG_DEBUG);
                
                out->messages[out->numMessages++] = transMsg;
            }
        } 
        //LOG_D(APP_TAG,"Destroy messages after transformChain 1");
            
        //msg_utils_destroyMsgBundle(messages);
        
        if(out->numMessages == 0)
        {
            LOG_D(APP_TAG,"Destroy out message bundle no messages");
            
            msg_utils_destroyMsgBundle(out);
            return NULL;
        }
        message_t * ret  = NULL;

        if(chain->aggregator != NULL)
        {
            if(out != NULL)
            {
                ret = chain->aggregator(ctx->user_context,out);
                LOG_D(APP_TAG,"Destroy message bundle after aggregator");
            
                msg_utils_destroyMsgBundle(out);
            }
        } else {
            if(out != NULL)
            {
                LOG_D(APP_TAG,"Before splitter aggregator");
            
                LOG_MSG_BUNDLE(APP_TAG,out);
                
                ret = msg_pipe_splitter_aggregrator(out);
                //LOG_D(APP_TAG,"Destroy message bundle after default aggregator");
            
                //msg_utils_destroyMsgBundle(out);
                LOG_D(APP_TAG,"After splitter aggregator");
            
                LOG_MSG_BUNDLE(APP_TAG,out);
                
            }
        }
        LOG_V(APP_TAG,"END - callTransformers");
    
        return ret;
    }  else {
        
        message_t * ret = msg_pipe_transformChain(ctx, client, chain, message);
        
        LOG_D(APP_TAG,"Destroy message after transformChain 2");
            
        msg_utils_destroyMsg(message);
        
        LOG_V(APP_TAG,"END - callTransformers");
    
        return ret;
    
    }
}
message_t * msg_pipe_callInputTransformers(msg_pipe_ctx_t *ctx, message_t *message)
{
    LOG_V(APP_TAG,"START - callInputTransformers");
    message_t * out = msg_pipe_callTransformers(ctx, ctx->in, ctx->in_chain, message);
    
    //msg_utils_destroyMsg(message);
        
    LOG_V(APP_TAG,"END - callInputTransformers");
    
    return out;
}
message_t * msg_pipe_callOutputTransformers(msg_pipe_ctx_t *ctx, message_t *message)
{
    LOG_V(APP_TAG,"START - callOutputTransformers");
    
    message_t * ret = msg_pipe_callTransformers(ctx, ctx->out, ctx->out_chain, message);
    
    //LOG_D(APP_TAG,"Destroy message after callTransformers");
            
    //msg_utils_destroyMsg(message);
        
    LOG_V(APP_TAG,"END - callOutputTransformers");
    return ret;
}
void msg_pipe_outboundPublish(msg_pipe_ctx_t * ctx, message_t * message)
{
    LOG_V(APP_TAG,"START - outboundPublish");
    messagingClient_t *outboundClient = ctx->out;
    if(outboundClient->connected == 0) 
    {
        msg_pipe_out_connect(ctx);
    }
    if(message != NULL) 
    {
        outboundClient->publish(outboundClient, message);
        LOG_D(APP_TAG,"Destroy message after outboundClient publish");
            
        msg_utils_destroyMsg(message);
    }
    LOG_V(APP_TAG,"END - outboundPublish");
    
}
void msg_pipe_inboundPublish(msg_pipe_ctx_t * ctx, message_t * message)
{
    LOG_V(APP_TAG,"START - inboundPublish");
    
    messagingClient_t *inboundClient = ctx->in;
    if(inboundClient->connected == 0) 
    {
        msg_pipe_in_connect(ctx);
    }
    if(message != NULL) 
    {
        inboundClient->publish(inboundClient, message);
        LOG_D(APP_TAG,"Destroy message after inboundClient publish");
            
        msg_utils_destroyMsg(message);
    }
    LOG_V(APP_TAG,"END - inboundPublish");
    
}

void msg_pipe_inboundSubscription(messagingClient_t *client, void * params, message_t * message)
{
    LOG_V(APP_TAG,"START - inboundSubscription");
    
    messagingClient_t *outboundClient = ((msg_pipe_ctx_t *) params)->out;
    message_t * out = message;
    msg_pipe_ctx_t * pipe = (msg_pipe_ctx_t *) params;
    
    if(pipe->in_chain != NULL)
    {
          out = msg_pipe_callInputTransformers(pipe, message);
    }
    if(out != NULL)
    {
        msg_pipe_outboundPublish(pipe,out);
    }
    LOG_V(APP_TAG,"END - inboundSubscription");
    
}
void msg_pipe_outboundSubscription(messagingClient_t *client, void * params, message_t * message)
{
    LOG_V(APP_TAG,"START - outboundSubscription");
    
    if(message == NULL || message->length ==0) return;

    messagingClient_t *inboundClient = ((msg_pipe_ctx_t *) params)->in;
    message_t * out = message;
    msg_pipe_ctx_t * pipe = (msg_pipe_ctx_t *) params;
    
    if(pipe->out_chain != NULL)
    {
        out = msg_pipe_callOutputTransformers(pipe, message);
    }
    if(out != NULL)
    {
        msg_pipe_inboundPublish(pipe, out);
    }
    LOG_V(APP_TAG,"END - outboundSubscription");
    
}

int msg_pipe_in_connect(msg_pipe_ctx_t * ctx)
{
    LOG_V(APP_TAG,"START - in connect");
    if(ctx->preInConnectHook != NULL) ctx->preInConnectHook(ctx);
    LOG_V(APP_TAG,"END - in connect");
    return ctx->in->connect(ctx->in);
}
int msg_pipe_out_connect(msg_pipe_ctx_t * ctx)
{
    LOG_V(APP_TAG,"START - out connect");
    if(ctx->preOutConnectHook != NULL) ctx->preOutConnectHook(ctx);
    LOG_V(APP_TAG,"END - out connect");
    return ctx->out->connect(ctx->out);
}
msg_pipe_ctx_t * msg_pipe(msg_pipe_settings_t settings) 
{
    msg_pipe_ctx_t * ctx = malloc(sizeof(msg_pipe_ctx_t));

    ctx->in = settings.in;
    ctx->out = settings.out;

    ctx->in_chain = settings.in_chain;
    ctx->out_chain = settings.out_chain;

    ctx->user_context = settings.user_context;
    
    ctx->loop = msg_pipe_loop;

    ctx->preOutConnectHook = settings.preOutConnectHook;
    ctx->preInConnectHook = settings.preInConnectHook;


    ctx->in->subscribe(ctx->in, ctx, msg_pipe_inboundSubscription);
    ctx->out->subscribe(ctx->out, ctx, msg_pipe_outboundSubscription);

    ctx->in->start(ctx->in);
    
    msg_pipe_in_connect(ctx);
    ctx->out->start(ctx->out);
    
    if(!settings.lazyConnect) msg_pipe_out_connect(ctx);
    
    return ctx;    
}
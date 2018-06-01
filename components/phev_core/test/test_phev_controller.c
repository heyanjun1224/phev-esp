#include "unity.h"
#include "msg_core.h"
#include "msg_tcpip.h"
#include "mock_msg_pipe.h"
#include "phev_controller.h"
#include "mock_phev_core.h"
#include "mock_msg_utils.h"


void test_handle_event(void)
{
    phevEvent_t event = {
        .type = CONNECT_REQUEST,
    };

    TEST_ASSERT_EQUAL(PHEV_OK, phev_controller_handleEvent(&event));
}

void test_phev_controller_init(void)
{
    messagingClient_t inClient;
    messagingClient_t outClient;

    msg_pipe_ctx_t pipe;

    msg_pipe_IgnoreAndReturn(&pipe);
    
    phevSettings_t settings = {
        .in     = &inClient,
        .out    = &outClient,
    };

    phevCtx_t * ctx = phev_controller_init(&settings);

    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_EQUAL(&pipe, ctx->pipe);
    TEST_ASSERT_EQUAL(0, ctx->queueSize);
} 

void test_phev_controller_init_set_phev_ctx(void)
{
    messagingClient_t inClient;
    messagingClient_t outClient;

    void * dummyCtx;

    msg_pipe_ctx_t pipe = {
        .user_context = dummyCtx,
    };

    msg_pipe_IgnoreAndReturn(&pipe);
    
    phevSettings_t settings = {
        .in     = &inClient,
        .out    = &outClient,
    };

    phevCtx_t * ctx = phev_controller_init(&settings);

    TEST_ASSERT_NOT_NULL(ctx);
    TEST_ASSERT_EQUAL(dummyCtx, ctx->pipe->user_context);
} 
void test_phev_controller_car_connection_config(void)
{
    char host[32];

    tcpip_ctx_t tcpip_ctx;

    messagingClient_t inClient = {
        .ctx = &tcpip_ctx,
    };
    messagingClient_t outClient;

    void * dummyCtx;

    msg_pipe_ctx_t pipe = {
        .in = &inClient,
        .user_context = dummyCtx,
    };

    msg_pipe_IgnoreAndReturn(&pipe);
    
    phevSettings_t settings = {
        .in     = &inClient,
        .out    = &outClient,
    };

    phevCtx_t * ctx = phev_controller_init(&settings);

    phev_controller_setCarConnectionConfig(ctx,"abc","123","127.0.0.1",8080);    

    TEST_ASSERT_NOT_NULL(ctx->config);
    TEST_ASSERT_EQUAL_STRING("abc", ctx->config->carConnectionWifi.ssid);
    TEST_ASSERT_EQUAL_STRING("123", ctx->config->carConnectionWifi.password);
    TEST_ASSERT_EQUAL_STRING("127.0.0.1", ctx->config->host);
    TEST_ASSERT_EQUAL(8080, ctx->config->port);

} 


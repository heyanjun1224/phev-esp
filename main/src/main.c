#ifdef ESP32
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_types.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"

#include "lwip/opt.h"

#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "apps/sntp/sntp.h"
#include "lwip/netif.h"

#include "esp_ota_ops.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "msg_mqtt.h"
#include "msg_gcp_mqtt.h"
#include "msg_pipe.h"
#include "msg_tcpip.h"
#include "msg_utils.h"

#include "phev_core.h"
#include "phev_controller.h"
#include "mqtt_client.h"

#include "jwt.h"

#include "json_bin.h"

#include "ppp_client.h"

//#define NO_PPP
//#define NO_OTA

#define GSM_RESET_PIN GPIO_NUM_21
#define UPDATE_NAME "update"
#define UPDATE_SSID "ssid"
#define UPDATE_PASSWORD "password"
#define UPDATE_HOST "host"
#define UPDATE_PATH "path"
#define UPDATE_PORT "port"
#define LATEST_BUILD "latestBuild"
#define UPDATE_OVER_GSM "overGsm"
#define FORCE_UPDATE "forceUpdate"

#ifndef BUILD_NUMBER
#define BUILD_NUMBER 1
#endif
typedef struct {
    int type;  // the type of timer's event
    int timer_group;
    int timer_idx;
    uint64_t timer_counter_value;
} timer_event_t;

extern const uint8_t rsa_private_pem_start[] asm("_binary_rsa_private2_pem_start");
extern const uint8_t rsa_private_pem_end[]   asm("_binary_rsa_private2_pem_end");

static bool connected = false;
esp_mqtt_client_handle_t client;

char *createJwt(const char *project_id);
static void wifi_conn_init_update(const char *ssid, const char * password);
static void wifi_conn_init(const char *ssid, const char * password);
void startTimer(void);

//#define CONFIG_WIFI_SSID "BTHub6-P535"
//#define CONFIG_WIFI_PASSWORD "S1mpsons"

//#define HOST_IP "35.205.234.94"
//#define HOST_PORT 8080

//#define HOST_IP "192.168.8.46"

#define CONFIG_WIFI_SSID "REMOTE45cfsc"
#define CONFIG_WIFI_PASSWORD "fhcm852767"

#define TIMER_DIVIDER         16  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define INTERVAL_SEC 1.00

#define CONNECTED_CLIENTS "connectedClients"
#define CAR_CONNECTION "carConnection"
#define CAR_SSID "ssid"
#define CAR_PASSWORD "password"
#define CAR_HOST "host"
#define CAR_PORT "port"

#define EXAMPLE_FILENAME "test"
#define EXAMPLE_SERVER_IP "192.168.1.1"
#define EXAMPLE_SERVER_PORT 8080

#define BUFFSIZE 1024
#define TEXT_BUFFSIZE 1024

static char ota_write_data[BUFFSIZE + 1] = { 0 };
/*an packet receive buffer*/
static char text[BUFFSIZE + 1] = { 0 };

static int binary_file_length = 0;

static EventGroupHandle_t wifi_event_group;
static EventGroupHandle_t mqtt_event_group;


xQueueHandle timer_queue;

const static int CONNECTED_BIT = BIT0;
const static char *APP_TAG = "Main";

void messagePublished(mqtt_event_handle_t event)
{
    ESP_LOGD(APP_TAG,"Publish Callback");
}

msg_mqtt_t mqtt = {
    .init = esp_mqtt_client_init,
    .start = esp_mqtt_client_start,
    .publish = esp_mqtt_client_publish,
    .subscribe = esp_mqtt_client_subscribe
};

static int socket_id = -1;

static int read_until(char *buffer, char delim, int len)
{
//  /*TODO: delim check,buffer check,further: do an buffer length limited*/
    int i = 0;
    while (buffer[i] != delim && i < len) {
        ++i;
    }
    return i + 1;
}

/* resolve a packet from http socket
 * return true if packet including \r\n\r\n that means http packet header finished,start to receive packet body
 * otherwise return false
 * */
static bool read_past_http_header(char text[], int total_len, esp_ota_handle_t update_handle)
{
    /* i means current position */
    int i = 0, i_read_len = 0;
    while (text[i] != 0 && i < total_len) {
        i_read_len = read_until(&text[i], '\n', total_len);
        // if we resolve \r\n line,we think packet header is finished
        if (i_read_len == 2) {
            int i_write_len = total_len - (i + 2);
            memset(ota_write_data, 0, BUFFSIZE);
            /*copy first http packet body to write buffer*/
            memcpy(ota_write_data, &(text[i + 2]), i_write_len);

            esp_err_t err = esp_ota_write( update_handle, (const void *)ota_write_data, i_write_len);
            if (err != ESP_OK) {
                ESP_LOGE(APP_TAG, "Error: esp_ota_write failed (%s)!", esp_err_to_name(err));
                return false;
            } else {
                ESP_LOGI(APP_TAG, "esp_ota_write header OK");
                binary_file_length += i_write_len;
            }
            return true;
        }
        i += i_read_len;
    }
    return false;
}
static bool connect_to_http_server(const char * host, const uint16_t port)
{
    ESP_LOGI(APP_TAG, "Server IP: %s Server Port:%d", host, port);

    int  http_connect_flag = -1;
    struct sockaddr_in sock_info;
    struct addrinfo *res;
    struct in_addr *addr;
    
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };

    int err = getaddrinfo(host , "80", &hints, &res);

    if(err != 0 || res == NULL) {
        ESP_LOGE(APP_TAG, "DNS lookup failed err=%d res=%p", err, res);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return false;
     }

        /* Code to print the resolved IP.

           Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
    addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    ESP_LOGI(APP_TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

    socket_id = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_id == -1) {
        ESP_LOGE(APP_TAG, "Create socket failed!");
        return false;
    }

    // set connect info
    memset(&sock_info, 0, sizeof(struct sockaddr_in));
    sock_info.sin_family = AF_INET;
    sock_info.sin_addr = *addr;
    sock_info.sin_port = htons(port);

    // connect to http server
    http_connect_flag = connect(socket_id, (struct sockaddr *)&sock_info, sizeof(sock_info));
    if (http_connect_flag == -1) {
        ESP_LOGE(APP_TAG, "Connect to server failed! errno=%d", errno);
        close(socket_id);
        return false;
    } else {
        ESP_LOGI(APP_TAG, "Connected to server");
        return true;
    }
    return false;
}

static void ota_task(void *pvParameter)
{
    phevConfig_t * config = (phevConfig_t *) pvParameter;

    if(config == NULL) 
    {
        ESP_LOGE(APP_TAG,"Config not passed to task");
        return;
    }

    esp_err_t err;
    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(APP_TAG, "Starting OTA ...");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) {
        ESP_LOGW(APP_TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 configured->address, running->address);
        ESP_LOGW(APP_TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(APP_TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);

    /* Wait for the callback to set the CONNECTED_BIT in the
       event group.
    */
    
    /*connect to http server*/
    if (connect_to_http_server(config->updateHost,config->updatePort)) {
        ESP_LOGI(APP_TAG, "Connected to http server");
    } else {
        ESP_LOGE(APP_TAG, "Connect to http server failed!");
        return;
    }

    /*send GET request to http server*/
    const char *GET_FORMAT =
        "GET %s HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "User-Agent: esp-idf/1.0 esp32\r\n\r\n";

    char *http_request = NULL;
    int get_len = asprintf(&http_request, GET_FORMAT, config->updateImageFullPath, config->updateHost, config->updatePort);
    if (get_len < 0) {
        ESP_LOGE(APP_TAG, "Failed to allocate memory for GET request buffer");
        return;
    }
    int res = send(socket_id, http_request, get_len, 0);
    free(http_request);

    if (res < 0) {
        ESP_LOGE(APP_TAG, "Send GET request to server failed %s", http_request);
        return;
    } else {
        ESP_LOGI(APP_TAG, "Send GET request to server succeeded");
    }

    update_partition = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(APP_TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, update_partition->address);
    assert(update_partition != NULL);

    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(APP_TAG, "esp_ota_begin succeeded");

    bool resp_body_start = false, socket_flag = true, http_200_flag = false;
    /*deal with all receive packet*/
    while (socket_flag) {
        memset(text, 0, TEXT_BUFFSIZE);
        memset(ota_write_data, 0, BUFFSIZE);
        int buff_len = recv(socket_id, text, TEXT_BUFFSIZE, 0);
        if (buff_len < 0) { /*receive error*/
            ESP_LOGE(APP_TAG, "Error: receive data error! errno=%d", errno);
            return;
        } else if (buff_len > 0 && !resp_body_start) {  /*deal with response header*/
            // only start ota when server response 200 state code
            if (strstr(text, "200") == NULL && !http_200_flag) {
                ESP_LOGE(APP_TAG, "ota url is invalid or bin is not exist");
                return;
            }
            http_200_flag = true;
            memcpy(ota_write_data, text, buff_len);
            resp_body_start = read_past_http_header(text, buff_len, update_handle);
        } else if (buff_len > 0 && resp_body_start) { /*deal with response body*/
            memcpy(ota_write_data, text, buff_len);
            err = esp_ota_write( update_handle, (const void *)ota_write_data, buff_len);
            if (err != ESP_OK) {
                ESP_LOGE(APP_TAG, "Error: esp_ota_write failed (%s)!", esp_err_to_name(err));
                return;
            }
            binary_file_length += buff_len;
            ESP_LOGD(APP_TAG, "Have written image length %d", binary_file_length);
        } else if (buff_len == 0) {  /*packet over*/
            socket_flag = false;
            ESP_LOGI(APP_TAG, "Connection closed, all packets received");
            close(socket_id);
        } else {
            ESP_LOGE(APP_TAG, "Unexpected recv result");
        }
    }

    ESP_LOGI(APP_TAG, "Total Write binary data length : %d", binary_file_length);

    if (esp_ota_end(update_handle) != ESP_OK) {
        ESP_LOGE(APP_TAG, "esp_ota_end failed!");
        return;
    }
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(APP_TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(APP_TAG, "Prepare to restart system!");
    esp_restart();
    return ;
}
void getIatExp(char *iat, char *exp, int time_size)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    while (timeinfo.tm_year < (2016 - 1900))
    {
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    snprintf(iat, time_size, "%lu", now);
    snprintf(exp, time_size, "%lu", now + 3600);
} 
char *createJwt(const char *project_id)
{
    char iat_time[sizeof(time_t) * 3 + 2];
    char exp_time[sizeof(time_t) * 3 + 2];
    const uint8_t *key = (unsigned char *) rsa_private_pem_start;
    size_t key_len = rsa_private_pem_end - rsa_private_pem_start;
    jwt_t *jwt = NULL;
    int ret = 0; 
    char *out = NULL;

    getIatExp(iat_time, exp_time, sizeof(iat_time));
    
    jwt_new(&jwt);

    ret = jwt_add_grant(jwt, "iat", iat_time);
    if (ret)
    {
        printf("Error setting issue timestamp: %d", ret);
        return NULL;
    }
    ret = jwt_add_grant(jwt, "exp", exp_time);
    if (ret)
    {
        printf("Error setting expiration: %d", ret);
        return NULL;
    }
    ret = jwt_add_grant(jwt, "aud", project_id);
    if (ret)
    {
        printf("Error adding audience: %d", ret);
        return NULL;
    }
    ret = jwt_set_alg(jwt, JWT_ALG_RS256, key, key_len);
    if (ret)
    {
        printf("Error during set alg: %d", ret);
        return NULL;
    }
    out = jwt_encode_str(jwt);
    jwt_free(jwt);  
    return out;
}

int global_sock = 0;

int connectSocket(const char *host, uint16_t port) 
{
    struct sockaddr_in addr;
    /* set up address to connect to */
    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = PP_HTONS(port);
    addr.sin_addr.s_addr = inet_addr(host);

    ESP_LOGI(APP_TAG,"Host %s Port %d",host,port);
  /* create the sockets */
    int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
  //  LWIP_ASSERT("sock >= 0", sock >= 0);

    if (sock == -1)
    {
        return -1;
    }
    int ret = lwip_connect(sock, (struct sockaddr *)(&addr), sizeof(addr));
    if(ret == -1)
    {
        return -1;
    }
  //  LWIP_ASSERT("ret == 0", ret == 0);
    ESP_LOGI(APP_TAG,"Connected to host %s port %d",host,port);
    
    global_sock = sock;
    return sock;
}
void my_ms_to_timeval(int timeout_ms, struct timeval *tv)
{
    tv->tv_sec = timeout_ms / 1000;
    tv->tv_usec = (timeout_ms - (tv->tv_sec * 1000)) * 1000;
}
static int tcp_poll_read(int soc, int timeout_ms)
{
    fd_set readset;
    FD_ZERO(&readset);
    FD_SET(soc, &readset);
    struct timeval timeout;
    my_ms_to_timeval(timeout_ms, &timeout);
    return select(soc + 1, &readset, NULL, NULL, &timeout);
}
static int tcp_read(int soc, uint8_t *buffer, int len, int timeout_ms)
{
    int poll = -1;
    if ((poll = tcp_poll_read(soc, timeout_ms)) <= 0) {
        return poll;
    }
    int read_len = read(soc, buffer, len);
    if (read_len == 0) {
        return -1;
    }
    return read_len;
}

int logRead(int soc, uint8_t * buf, size_t len)
{

    int num = tcp_read(soc,buf,len,10000);

    if(num > 0) 
    {
        ESP_LOG_BUFFER_HEXDUMP(APP_TAG,buf,num,ESP_LOG_DEBUG);
    }
    return num;
}
int logWrite(int soc, uint8_t * buf, size_t len)
{
    ESP_LOG_BUFFER_HEXDUMP(APP_TAG,buf,len,ESP_LOG_DEBUG);
    int num = lwip_write(soc,buf,len);
    ESP_LOGD(APP_TAG, "Written %d bytes",num);
    
    return num;
}

char * getConfigString(cJSON * json, char * option) 
{
    cJSON * value = cJSON_GetObjectItemCaseSensitive(json, option);
    if(value == NULL) {
        ESP_LOGE(APP_TAG,"Cannot find option %s", option);
        return NULL;
    }

    ESP_LOGI(APP_TAG,"Option %s set to %s", option, value->valuestring);
    
    return value->valuestring;
}
uint16_t getConfigInt(cJSON * json, char * option) 
{
    cJSON * value = cJSON_GetObjectItemCaseSensitive(json, option);
    if(value == NULL) {
        ESP_LOGE(APP_TAG,"Cannot find option %s", option);
        return NULL;
    }

    ESP_LOGI(APP_TAG,"Option %s set to %d", option, value->valueint);
    
    return value->valueint;
}

long getConfigLong(cJSON * json, char * option) 
{
    cJSON * value = cJSON_GetObjectItemCaseSensitive(json, option);
    if(value == NULL) {
        ESP_LOGE(APP_TAG,"Cannot find option %s", option);
        return 0;
    }

    ESP_LOGI(APP_TAG,"Option %s set to %ld", option, (long) value->valuedouble);
    
    return (long) value->valuedouble;
}
bool getConfigBool(cJSON * json, char * option) 
{
    cJSON * value = cJSON_GetObjectItemCaseSensitive(json, option);
    if(value == NULL) {
        ESP_LOGW(APP_TAG,"Cannot find option %s defaulting to false", option);
        return false;
    }

    if(cJSON_IsTrue(value)) {
        ESP_LOGI(APP_TAG,"Option %s set to true", option);
    } else {
        ESP_LOGI(APP_TAG,"Option %s set to false", option);
    }
    
    return cJSON_IsTrue(value);
}
    
void checkForUpdate(phevCtx_t * ctx, cJSON * json)
{
    cJSON * update = cJSON_GetObjectItemCaseSensitive(json, UPDATE_NAME);

    if(update == NULL)
    {
        ESP_LOGE(APP_TAG, "Cannot find update config");
    }
    
    long build = getConfigLong(json,LATEST_BUILD);

    phev_controller_setUpdateConfig(ctx, getConfigString(update,UPDATE_SSID), 
                                        getConfigString(update,UPDATE_PASSWORD),
                                        getConfigString(update,UPDATE_HOST),
                                        getConfigString(update,UPDATE_PATH),
                                        getConfigInt(update,UPDATE_PORT),
                                        build
                                    );
    
    bool forceUpdate = getConfigBool(json, FORCE_UPDATE);
    
    if(build > BUILD_NUMBER || forceUpdate)
    {
        ESP_LOGI(APP_TAG,"Updating firmware to build %ld from %d",build, BUILD_NUMBER);
        if(!getConfigBool(update,UPDATE_OVER_GSM))
        {
            ESP_LOGI(APP_TAG,"Connect over wifi to update");
            wifi_conn_init_update(ctx->config->updateWifi.ssid,ctx->config->updateWifi.password);
            xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);
            ESP_LOGI(APP_TAG, "Connected to Wifi");

        }
        ESP_LOGI(APP_TAG,"Build image %s",ctx->config->updateImageFullPath);
        ota_task((void *) ctx->config);
        ESP_LOGE(APP_TAG, "update failed better restart system!");
        esp_restart();
    
    } else {
        ESP_LOGI(APP_TAG,"At latest firmware");
    }  
}
message_t * transformJSONToHex(void * ctx, message_t *message)
{
    phevCtx_t * phevCtx = (phevCtx_t *) ctx;
    cJSON *json = cJSON_Parse((const char *) message->data);

#ifndef NO_OTA
    checkForUpdate(phevCtx,json);
#endif
    cJSON *connect = NULL;

    connect = cJSON_GetObjectItemCaseSensitive(json, CONNECTED_CLIENTS);

    if(connect == NULL) 
    {
        return NULL;
    }
    if (connect->valueint > 0)
    {
        cJSON * carConnection = cJSON_GetObjectItemCaseSensitive(json, CAR_CONNECTION);
    
        phev_controller_setCarConnectionConfig(phevCtx, getConfigString(carConnection,CAR_SSID), 
                                                    getConfigString(carConnection,CAR_PASSWORD),
                                                    getConfigString(carConnection,CAR_HOST),
                                                    getConfigInt(carConnection,CAR_PORT)
                                                    );
        message_t out;
        uint8_t * data;
        //uint8_t mac[] = {0x24, 0x0d, 0xc2, 0xc2, 0x91, 0x85};
        uint8_t mac[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        
        cJSON_Delete(json);

        return phev_core_startMessageEncoded(2,mac);
    }
    cJSON_Delete(json);
   
    return NULL;
}
message_t * transformHexToJSON(void * ctx, phevMessage_t *message)
{
    char * output;
    
    cJSON * response = cJSON_CreateObject();
    if(response == NULL) 
    {
        ESP_LOGE(APP_TAG,"Cannot create JSON response");
        return NULL;
    }
    cJSON * command = cJSON_CreateNumber(message->command);
    if(command == NULL) 
    {
        ESP_LOGE(APP_TAG,"Cannot create JSON command response");
        return NULL;
    }
    cJSON_AddItemToObject(response, "command", command);
    
    cJSON * type = NULL;
    if(message->type == REQUEST_TYPE)
    {
        type = cJSON_CreateString("request");
    } else {
        type = cJSON_CreateString("response");
    }
    if(type == NULL) 
    {
        ESP_LOGE(APP_TAG,"Cannot create JSON type response");
        return NULL;
    }
    
    cJSON_AddItemToObject(response, "type", type);
    
    cJSON * length = cJSON_CreateNumber(message->length);
    if(length == NULL) 
    {
        ESP_LOGE(APP_TAG,"Cannot create JSON length response");
        return NULL;
    }
    cJSON_AddItemToObject(response, "length", length);  

    cJSON * data = cJSON_CreateArray();
    if(data == NULL) 
    {
        ESP_LOGE(APP_TAG,"Cannot create JSON data array response");
        return NULL;
    }
    cJSON_AddItemToObject(response, "data", data);  

    for(int i=0; i < message->length - 3; i++)
    {
        cJSON * item = cJSON_CreateNumber(message->data[i]);
        if (item == NULL)
        {
            return NULL;
        }
        cJSON_AddItemToArray(data, item);
    }
    
    output = cJSON_Print(response); 

    cJSON_Delete(response);

    message_t * outputMessage = msg_utils_createMsg((uint8_t *) output, strlen(output));
    free(output);
    //ESP_LOGI(APP_TAG,"%s",output);
    return outputMessage;
}
int connectToCar(const char *host, uint16_t port)
{
    return connectSocket(host,port);
}
phevCtx_t * connectPipe(void)
{
    gcpSettings_t inSettings = {
        .host = "mqtt.googleapis.com",
        .port = 8883,
        .clientId = "projects/phev-db3fa/locations/us-central1/registries/my-registry/devices/my-device2",
        .device = "my-device2",
        .createJwt = createJwt,
        .mqtt = &mqtt,
        .projectId = "phev-db3fa",
        .topic = "/devices/my-device2/events",
        .published = messagePublished,
    }; 
    
    tcpIpSettings_t outSettings = {
        .connect = connectToCar, 
        .read = logRead,
        .write = logWrite,
    };
    
     
    phevSettings_t phev_settings = {
        .in = msg_gcp_createGcpClient(inSettings),
        .out = msg_tcpip_createTcpIpClient(outSettings),
        .inputTransformer = transformJSONToHex,
        .outputTransformer = transformHexToJSON,
        .startWifi = wifi_conn_init,
    };

    return phev_controller_init(&phev_settings);
}

void ping_task(void *pvParameter)
{
    phevCtx_t *ctx = (phevCtx_t *) pvParameter;

    ESP_LOGI(APP_TAG,"Ping task started");
    
    while(1)
    {
        if(ctx->currentPing > 0) phev_controller_resetPing(ctx);
        
        while(ctx->pipe->out->connected)
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            phev_controller_ping(ctx);
            ESP_LOGD(APP_TAG, "Free heap %d", system_get_free_heap_size());
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}
void main_loop(void *pvParameter)
{
       
    phevCtx_t *ctx = NULL;
    {
        ESP_LOGI(APP_TAG,"Waiting to connect...");
        ctx = connectPipe();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    } while(!ctx->pipe->in->connected);
    
    ESP_LOGI(APP_TAG,"TCPIP connected %d MQTT connected %d",ctx->pipe->out->connected,ctx->pipe->in->connected);
    xTaskCreate(&ping_task, "ping_task", 4096, (void *) ctx, 5, NULL);   
    while(1)
    {
        msg_pipe_loop(ctx->pipe);
        if(!ctx->pipe->in->connected)
        {
            ESP_LOGW(APP_TAG,"MQTT connection dropped");
            vTaskDelay(10000 / portTICK_PERIOD_MS);
            ctx->pipe->in->connect(ctx->pipe->in);

        }   
        vTaskDelay(100 / portTICK_PERIOD_MS);
            
    }

    ESP_LOGI(APP_TAG,"Disconnected...");
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    ESP_LOGI(APP_TAG,"Restarting...");
    esp_restart();
} 
static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id)
    {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        ESP_LOGI(APP_TAG, "Wifi started");
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
               auto-reassociate. */
        //esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        esp_restart();
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void wifi_conn_init(const char * wifiSSID, const char * wifiPassword)
{
    esp_wifi_stop();
    wifi_config_t wifi_config = {
        .sta.ssid = "",
        .sta.password = "",
    };
    //memset(&wifi_config,0, sizeof(wifi_config_t));
    strncpy((char *) wifi_config.sta.ssid, wifiSSID,strlen(wifiSSID));
    strncpy((char *) wifi_config.sta.password, wifiPassword,strlen(wifiPassword));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(APP_TAG, "start the WIFI SSID:[%s] password:[%s]", wifiSSID, wifiPassword);
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);
    for (struct netif *pri = netif_list; pri != NULL; pri=pri->next)
    {
        ESP_LOGD(APP_TAG, "Interface priority is %c%c%d (" IPSTR "/" IPSTR " gateway " IPSTR ")",
        pri->name[0], pri->name[1], pri->num,
        IP2STR(&pri->ip_addr.u_addr.ip4), IP2STR(&pri->netmask.u_addr.ip4), IP2STR(&pri->gw.u_addr.ip4));
        if(pri->name[0] == 'p') netif_set_default(pri);
        //    SetDNSServer(dns);
        //    return;
    }
}

static void wifi_conn_init_update(const char * wifiSSID, const char * wifiPassword)
{
    esp_wifi_stop();
    wifi_config_t wifi_config = {
        .sta.ssid = "",
        .sta.password = "",
    };
    //memset(&wifi_config,0, sizeof(wifi_config_t));
    strncpy((char *) wifi_config.sta.ssid, wifiSSID,strlen(wifiSSID));
    strncpy((char *) wifi_config.sta.password, wifiPassword,strlen(wifiPassword));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_LOGI(APP_TAG, "start the WIFI SSID:[%s] password:[%s]", wifiSSID, wifiPassword);
    ESP_ERROR_CHECK(esp_wifi_start());
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                        false, true, portMAX_DELAY);
        for (struct netif *pri = netif_list; pri != NULL; pri=pri->next)
    {
        ESP_LOGD(APP_TAG, "Interface priority is %c%c%d (" IPSTR "/" IPSTR " gateway " IPSTR ")",
        pri->name[0], pri->name[1], pri->num,
        IP2STR(&pri->ip_addr.u_addr.ip4), IP2STR(&pri->netmask.u_addr.ip4), IP2STR(&pri->gw.u_addr.ip4));
     //   if(pri->name[0] == 'p') netif_set_default(pri);
        //    SetDNSServer(dns);
        //    return;
    }
}
static void sntp_task(void)
{
    int retry = 0;
    const int retry_count = 100;
    time_t now;
    struct tm timeinfo;

    ESP_LOGI(APP_TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count)
    {
        ESP_LOGI(APP_TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    ESP_LOGI(APP_TAG, "Time synced");
}
void wifiSetup(void)
{
    uint8_t new_mac[8] = {0x24, 0x0d, 0xc2, 0xc2, 0x91, 0x85};
    esp_base_mac_addr_set(new_mac);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
}
void start_app(void)
{
    ESP_LOGI(APP_TAG, "Application starting...");
    
    wifiSetup();

    vTaskDelay(2000 / portTICK_PERIOD_MS);
#ifndef NO_PPP    
    ppp_main();
#else
    wifi_conn_init(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
#endif
    sntp_task();
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    xTaskCreate(&main_loop, "main_task", 4096, NULL, 5, NULL);    

}
void IRAM_ATTR timer_group0_isr(void *para)
{
    int timer_idx = (int) para;

    //ESP_LOGI(APP_TAG,"Ping!!!");
    /* Retrieve the interrupt status and the counter value
       from the timer that reported the interrupt */
    uint32_t intr_status = TIMERG0.int_st_timers.val;
    TIMERG0.hw_timer[timer_idx].update = 1;
    uint64_t timer_counter_value = 
        ((uint64_t) TIMERG0.hw_timer[timer_idx].cnt_high) << 32
        | TIMERG0.hw_timer[timer_idx].cnt_low;

    /* Prepare basic event data
       that will be then sent back to the main program task */
    timer_event_t evt;
    evt.timer_group = 0;
    evt.timer_idx = timer_idx;
    evt.timer_counter_value = timer_counter_value;

    /* Clear the interrupt
       and update the alarm time for the timer with without reload */
    if ((intr_status & BIT(timer_idx)) && timer_idx == TIMER_0) {
        evt.type = 0;
        TIMERG0.int_clr_timers.t0 = 1;
        timer_counter_value += (uint64_t) (INTERVAL_SEC * TIMER_SCALE);
        TIMERG0.hw_timer[timer_idx].alarm_high = (uint32_t) (timer_counter_value >> 32);
        TIMERG0.hw_timer[timer_idx].alarm_low = (uint32_t) timer_counter_value;
    } else if ((intr_status & BIT(timer_idx)) && timer_idx == TIMER_1) {
        evt.type = 0;
        TIMERG0.int_clr_timers.t1 = 1;
    } else {
        evt.type = -1; // not supported even type
    }

    /* After the alarm has been triggered
      we need enable it again, so it is triggered the next time */
    TIMERG0.hw_timer[timer_idx].config.alarm_en = TIMER_ALARM_EN;

    /* Now just send the event data back to the main program task */
    xQueueSendFromISR(timer_queue, &evt, NULL);
}

void startTimer(void)
{
    timer_queue = xQueueCreate(10, sizeof(timer_event_t));
    
    timer_config_t config;
    config.divider = TIMER_DIVIDER;
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_EN;
    config.intr_type = TIMER_INTR_LEVEL;
    config.auto_reload = 0;
    timer_init(TIMER_GROUP_0, TIMER_0, &config);

    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0x00000000ULL);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, INTERVAL_SEC * TIMER_SCALE);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_register(TIMER_GROUP_0, TIMER_0, timer_group0_isr, 
        (void *) TIMER_0, ESP_INTR_FLAG_IRAM, NULL);

    timer_start(TIMER_GROUP_0, TIMER_0);

}
 
void resetGSMModule(int resetPin)
{
    ESP_LOGI(APP_TAG,"Resetting GSM module");
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = 1ULL << resetPin;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);
    gpio_set_level(resetPin, 0);
    //ESP_LOGI(APP_TAG,"Before Level %d", gpio_get_level(GPIO_NUM_19));
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    gpio_set_level(resetPin, 1);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    //ESP_LOGI(APP_TAG,"After Level %d", gpio_get_level(GPIO_NUM_19));
    gpio_set_level(resetPin, 0);
}
void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );
    resetGSMModule(GPIO_NUM_21);
    tcpip_adapter_init();

    //timer_example_evt_task(NULL);
    
    ESP_LOGI(APP_TAG,"PHEV ESP Build %d", BUILD_NUMBER);
    
    start_app();
}
#endif
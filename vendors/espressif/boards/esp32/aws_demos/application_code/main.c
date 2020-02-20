/*
 * Amazon FreeRTOS V1.4.7
 * Copyright (C) 2018 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

#include "iot_config.h"

/* FreeRTOS includes. */

#include "FreeRTOS.h"
#include "task.h"

/* Demo includes */
#include "aws_demo.h"
#include "aws_dev_mode_key_provisioning.h"

#ifndef LIBRARY_LOG_LEVEL
#define LIBRARY_LOG_LEVEL IOT_LOG_DEBUG
#endif

#ifndef LIBRARY_LOG_NAME
#define LIBRARY_LOG_NAME "main_c"
#endif

/* AWS System includes. */
#include "bt_hal_manager.h"
#include "iot_system_init.h"
#include "iot_logging_setup.h"
#include "iot_logging_task.h"

#include "nvs_flash.h"
#if !AFR_ESP_LWIP
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#endif

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_interface.h"
#include "esp_bt.h"
#if CONFIG_NIMBLE_ENABLED == 1
#include "esp_nimble_hci.h"
#else
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#endif

#include "driver/uart.h"
#include "aws_application_version.h"
#include "tcpip_adapter.h"

#include "iot_network_manager_private.h"
#include "iot_init.h"

#include "driver/gpio.h"

#if BLE_ENABLED
#include "bt_hal_manager_adapter_ble.h"
#include "bt_hal_manager.h"
#include "bt_hal_gatt_server.h"

#include "iot_ble.h"
#include "iot_ble_config.h"
#include "iot_ble_wifi_provisioning.h"
#include "iot_ble_numericComparison.h"
#endif

/* Logging Task Defines. */
#define mainLOGGING_MESSAGE_QUEUE_LENGTH (32)
#define mainLOGGING_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 4)
#define mainDEVICE_NICK_NAME "Espressif_Demo"

QueueHandle_t spp_uart_queue = NULL;

/* Variable used to indicate the connected network. */
static uint32_t puConnectedNetwork = AWSIOT_NETWORK_TYPE_NONE;

/* Semaphore used to wait for a network to be available. */
static IotSemaphore_t xNetworkSemaphore;

static IotNetworkManagerSubscription_t subscription = IOT_NETWORK_MANAGER_SUBSCRIPTION_INITIALIZER;

/* Static arrays for FreeRTOS+TCP stack initialization for Ethernet network connections
 * are use are below. If you are using an Ethernet connection on your MCU device it is
 * recommended to use the FreeRTOS+TCP stack. The default values are defined in
 * FreeRTOSConfig.h. */

/**
 * @brief Initializes the board.
 */
static void prvMiscInitialization(void);

#if BLE_ENABLED
/* Initializes bluetooth */
static esp_err_t prvBLEStackInit(void);
/** Helper function to teardown BLE stack. **/
esp_err_t xBLEStackTeardown(void);
static void spp_uart_init(void);
#endif

/*-----------------------------------------------------------*/
int vInitialize();

/**
 * @brief Application runtime entry point.
 */
int app_main(void)
{
    /* Perform any hardware initialization that does not require the RTOS to be
     * running.  */

    prvMiscInitialization();

    if (SYSTEM_Init() == pdPASS)
    {
        /* A simple example to demonstrate key and certificate provisioning in
         * microcontroller flash using PKCS#11 interface. This should be replaced
         * by production ready key provisioning mechanism. */
        vDevModeKeyProvisioning();
#if BLE_ENABLED
        /* Initialize BLE. */
        ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

        if (prvBLEStackInit() != ESP_OK)
        {
            configPRINTF(("Failed to initialize the bluetooth stack\n "));

            while (1)
            {
            }
        }
#else
        ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
        ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
#endif /* if BLE_ENABLED */
        if (vInitialize() == pdTRUE)
        {
            configPRINTF(("Network Inititalized - now running the demos\n "));
            DEMO_RUNNER_RunDemos();
        }
    }

    /* Start the scheduler.  Initialization that requires the OS to be running,
     * including the WiFi initialization, is performed in the RTOS daemon task
     * startup hook. */
    /* Following is taken care by initialization code in ESP IDF */
    return 0;
}

/*-----------------------------------------------------------*/
extern void vApplicationIPInit(void);
static void prvMiscInitialization(void)
{

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();

    if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

#if BLE_ENABLED
    NumericComparisonInit();
    spp_uart_init();
#endif

    /* Create tasks that are not dependent on the WiFi being initialized. */
    xLoggingTaskInitialize(mainLOGGING_TASK_STACK_SIZE,
                           tskIDLE_PRIORITY + 5,
                           mainLOGGING_MESSAGE_QUEUE_LENGTH);

#if AFR_ESP_LWIP
    configPRINTF(("Initializing lwIP TCP stack\r\n"));
    tcpip_adapter_init();
#else
    configPRINTF(("Initializing FreeRTOS TCP stack\r\n"));
    vApplicationIPInit();
#endif
}

/*-----------------------------------------------------------*/

#if BLE_ENABLED

#if CONFIG_NIMBLE_ENABLED == 1
esp_err_t prvBLEStackInit(void)
{
    return ESP_OK;
}

esp_err_t xBLEStackTeardown(void)
{
    esp_err_t xRet;

    xRet = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);

    return xRet;
}

#else  /* if CONFIG_NIMBLE_ENABLED == 1 */

static esp_err_t prvBLEStackInit(void)
{
    return ESP_OK;
}

esp_err_t xBLEStackTeardown(void)
{
    esp_err_t xRet = ESP_OK;

    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED)
    {
        xRet = esp_bluedroid_disable();
    }

    if (xRet == ESP_OK)
    {
        xRet = esp_bluedroid_deinit();
    }

    if (xRet == ESP_OK)
    {
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
        {
            xRet = esp_bt_controller_disable();
        }
    }

    if (xRet == ESP_OK)
    {
        xRet = esp_bt_controller_deinit();
    }

    if (xRet == ESP_OK)
    {
        xRet = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    }

    if (xRet == ESP_OK)
    {
        xRet = esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
    }

    return xRet;
}
#endif /* if CONFIG_NIMBLE_ENABLED == 1 */
#endif /* if BLE_ENABLED */

/*-----------------------------------------------------------*/

#if BLE_ENABLED
static void spp_uart_init(void)
{
    uart_config_t uart_config =
        {
            .baud_rate = 115200,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_RTS,
            .rx_flow_ctrl_thresh = 122,
        };

    /* Set UART parameters */
    uart_param_config(UART_NUM_0, &uart_config);
    /*Set UART pins */
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    /*Install UART driver, and get the queue. */
    uart_driver_install(UART_NUM_0, 4096, 8192, 10, &spp_uart_queue, 0);
}

/*-----------------------------------------------------------*/

BaseType_t getUserMessage(INPUTMessage_t *pxINPUTmessage,
                          TickType_t xAuthTimeout)
{
    uart_event_t xEvent;
    BaseType_t xReturnMessage = pdFALSE;

    if (xQueueReceive(spp_uart_queue, (void *)&xEvent, (portTickType)xAuthTimeout))
    {
        switch (xEvent.type)
        {
        /*Event of UART receiving data */
        case UART_DATA:

            if (xEvent.size)
            {
                pxINPUTmessage->pcData = (uint8_t *)malloc(sizeof(uint8_t) * xEvent.size);

                if (pxINPUTmessage->pcData != NULL)
                {
                    memset(pxINPUTmessage->pcData, 0x0, xEvent.size);
                    uart_read_bytes(UART_NUM_0, (uint8_t *)pxINPUTmessage->pcData, xEvent.size, portMAX_DELAY);
                    xReturnMessage = pdTRUE;
                }
                else
                {
                    configPRINTF(("Malloc failed in main.c\n"));
                }
            }

            break;

        default:
            break;
        }
    }

    return xReturnMessage;
}
#endif /* if BLE_ENABLED */

/*-----------------------------------------------------------*/

extern void esp_vApplicationTickHook();
void IRAM_ATTR vApplicationTickHook()
{
    esp_vApplicationTickHook();
}

/*-----------------------------------------------------------*/
extern void esp_vApplicationIdleHook();
void vApplicationIdleHook()
{
    esp_vApplicationIdleHook();
}

/*-----------------------------------------------------------*/

void vApplicationDaemonTaskStartupHook(void)
{
}

#if !AFR_ESP_LWIP
/*-----------------------------------------------------------*/
void vApplicationIPNetworkEventHook(eIPCallbackEvent_t eNetworkEvent)
{
    uint32_t ulIPAddress, ulNetMask, ulGatewayAddress, ulDNSServerAddress;
    system_event_t evt;

    if (eNetworkEvent == eNetworkUp)
    {
        /* Print out the network configuration, which may have come from a DHCP
         * server. */
        FreeRTOS_GetAddressConfiguration(
            &ulIPAddress,
            &ulNetMask,
            &ulGatewayAddress,
            &ulDNSServerAddress);

        evt.event_id = SYSTEM_EVENT_STA_GOT_IP;
        evt.event_info.got_ip.ip_changed = true;
        evt.event_info.got_ip.ip_info.ip.addr = ulIPAddress;
        evt.event_info.got_ip.ip_info.netmask.addr = ulNetMask;
        evt.event_info.got_ip.ip_info.gw.addr = ulGatewayAddress;
        esp_event_send(&evt);
    }
}
#endif

void vLedBlinkCallback(TimerHandle_t xTimer)
{
    GPIO_OUTPUT_SET(32, 1);
    vTaskDelay(pdMS_TO_TICKS(500UL));
    GPIO_OUTPUT_SET(32, 0);
}

void vNetworkChangedCB(uint32_t ulNetworkType,
                       AwsIotNetworkState_t xNetworkState,
                       void *pvContext)
{
    if ((xNetworkState == eNetworkStateEnabled) && (puConnectedNetwork == AWSIOT_NETWORK_TYPE_NONE))
    {
        puConnectedNetwork = ulNetworkType;
        IotSemaphore_Post(&xNetworkSemaphore);
    }
}

int vInitialize()
{
    {
        int status = EXIT_SUCCESS;
        bool commonLibrariesInitialized = false;
        bool semaphoreCreated = false;

        /* Initialize common libraries required by network manager and demo. */
        if (IotSdk_Init() == true)
        {
            commonLibrariesInitialized = true;
        }
        else
        {
            IotLogInfo("Failed to initialize the common library.");
            status = EXIT_FAILURE;
        }

        if (status == EXIT_SUCCESS)
        {
            if (AwsIotNetworkManager_Init() != pdTRUE)
            {
                IotLogError("Failed to initialize network manager library.");
                status = EXIT_FAILURE;
            }
        }

        if (status == EXIT_SUCCESS)
        {
            /* Create semaphore to signal that a network is available for the demo. */
            if (IotSemaphore_Create(&xNetworkSemaphore, 0, 1) != true)
            {
                IotLogError("Failed to create semaphore to wait for a network connection.");
                status = EXIT_FAILURE;
            }
            else
            {
                semaphoreCreated = true;
            }
        }

        if (status == EXIT_SUCCESS)
        {
            /* Subscribe for network state change from Network Manager. */
            if (AwsIotNetworkManager_SubscribeForStateChange(AWSIOT_NETWORK_TYPE_WIFI,
                                                             vNetworkChangedCB,
                                                             NULL,
                                                             &subscription) != pdTRUE)
            {
                IotLogError("Failed to subscribe network state change callback.");
                status = EXIT_FAILURE;
            }
        }

        /* Initialize all the  networks configured for the device. */
        if (status == EXIT_SUCCESS)
        {
            configPRINTF(("Connecting to network\r\n"));
            if (AwsIotNetworkManager_EnableNetwork(configENABLED_NETWORKS) != configENABLED_NETWORKS)
            {
                IotLogError("Failed to intialize all the networks configured for the device.");
                configPRINTF((" .... RESETING ............ \r\n"));
                esp_restart();
                status = EXIT_FAILURE;
            }
        }

        if (status == EXIT_SUCCESS)
        {
            /* Wait for network configured for the demo to be initialized. */
            puConnectedNetwork = AwsIotNetworkManager_GetConnectedNetworks() & AWSIOT_NETWORK_TYPE_WIFI;

            if (puConnectedNetwork == AWSIOT_NETWORK_TYPE_NONE)
            {
                TimerHandle_t xTimer = xTimerCreate("led_timer", pdMS_TO_TICKS(1000UL), pdTRUE, 0, vLedBlinkCallback);

                if (xTimer == NULL)
                {
                    configPRINTF(("Unable to create the timer for led blinking\n"));
                }
                else
                {
                    xTimerStart(xTimer, 0);
                }
                /* Network not yet initialized. Block for a network to be intialized. */
                IotLogInfo("No networks connected for the demo. Waiting for a network connection. ");
                IotSemaphore_Wait(&xNetworkSemaphore);
                puConnectedNetwork = AwsIotNetworkManager_GetConnectedNetworks() & AWSIOT_NETWORK_TYPE_WIFI;
                xTimerStop(xTimer, 0);
            }
        }

        if (status == EXIT_FAILURE)
        {
            configPRINTF((" .... RESETING ............ \r\n"));
            esp_restart();

            if (semaphoreCreated == true)
            {
                IotSemaphore_Destroy(&xNetworkSemaphore);
            }

            if (commonLibrariesInitialized == true)
            {
                IotSdk_Cleanup();
            }
        }

        return status == 0 ? pdPASS : pdFAIL;
    }
}
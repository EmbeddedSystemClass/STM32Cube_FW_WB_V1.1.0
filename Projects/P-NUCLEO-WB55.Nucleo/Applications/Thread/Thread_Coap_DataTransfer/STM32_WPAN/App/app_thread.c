/**
 ******************************************************************************
 * @file    app_thread.c
 * @author  MCD Application Team
 * @brief   Thread Application
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2019 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by ST under Ultimate Liberty license
 * SLA0044, the "License"; You may not use this file except in compliance with
 * the License. You may obtain a copy of the License at:
 *               www.st.com/SLA0044
 *
 ******************************************************************************
 */


/* Includes ------------------------------------------------------------------*/
#include "app_common.h"
#include "utilities_common.h"
#include "app_entry.h"
#include "scheduler.h"
#include "dbg_trace.h"
#include "app_thread.h"
#include "stm32wbxx_core_interface_def.h"
#include "openthread_api_wb.h"
#include "shci.h"
#include "stm_logging.h"
#include "app_conf.h"
#if (CFG_USB_INTERFACE_ENABLE != 0)
#include "vcp.h"
#include "vcp_conf.h"
#endif /* (CFG_USB_INTERFACE_ENABLE != 0) */

#include "data_transfer.h"

/* Private defines -----------------------------------------------------------*/
#define C_SIZE_CMD_STRING     256U
#define C_PANID               0x2226U
#define C_CHANNEL_NB          19U

#define C_RESSOURCE_DATA_TRANSFER   "dataTransfer"
#define C_RESSOURCE_Provisioning    "provisioning"

/* Private macros ------------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/
static void APP_THREAD_CheckWirelessFirmwareInfo(void);
static void APP_THREAD_DeviceConfig(void);
static void APP_THREAD_StateNotif(uint32_t NotifFlags, void *pContext);
static void APP_THREAD_TraceError(const char * pMess, uint32_t ErrCode);

static void Send_CLI_To_M0(void);
static void Send_CLI_Ack_For_OT(void);
static void HostTxCb( void );
static void Wait_Getting_Ack_From_M0(void);
static void Receive_Ack_From_M0(void);
static void Receive_Notification_From_M0(void);
#if (CFG_USB_INTERFACE_ENABLE != 0)
static uint32_t ProcessCmdString(uint8_t* buf , uint32_t len);
#else
static void RxCpltCallback(void);
#endif

static void APP_THREAD_CheckMsgValidity(void);
static void APP_THREAD_SendNextBuffer(void);
static void APP_THREAD_DummyReqHandler(void * p_context,
    otCoapHeader * pHeader,
    otMessage * pMessage,
    const otMessageInfo * pMessageInfo);
static void APP_THREAD_CoapDataReqHandler(otCoapHeader * pHeader,
    otMessage * pMessage,
    const otMessageInfo * pMessageInfo);
static void APP_THREAD_SendDataResponse(otCoapHeader * pRequestHeader,
    const otMessageInfo * pMessageInfo);
static void APP_THREAD_ProvisioningReqHandler(otCoapHeader * pHeader,
    otMessage * pMessage,
    const otMessageInfo * pMessageInfo);
static otError APP_THREAD_ProvisioningRespSend(otCoapHeader* pRequestHeader,
    const otMessageInfo * pMessageInfo);
static void APP_THREAD_ProvisioningReqSend(void);
static void APP_THREAD_ProvisioningRespHandler(otCoapHeader * pHeader,
    otMessage * pMessage,
    const otMessageInfo * pMessageInfo,
    otError Result);
static void APP_THREAD_SendCoapUnicastRequest(void);
static void APP_THREAD_DataRespHandler(otCoapHeader  * pHeader,
    otMessage * pMessage,
    const otMessageInfo * pMessageInfo,
    otError Result);
static void APP_THREAD_DummyRespHandler(void * p_context,
    otCoapHeader * pHeader,
    otMessage * pMessage,
    const otMessageInfo * pMessageInfo,
    otError Result);
static void APP_THREAD_AskProvisioning(void);

/* Private variables -----------------------------------------------*/
#if (CFG_USB_INTERFACE_ENABLE != 0)
  static uint8_t TmpString[C_SIZE_CMD_STRING];
static uint8_t VcpRxBuffer[sizeof(TL_CmdSerial_t)];        /* Received Data over USB are stored in this buffer */
static uint8_t VcpTxBuffer[sizeof(TL_EvtPacket_t) + 254U]; /* Transmit buffer over USB */
#else
static uint8_t aRxBuffer[C_SIZE_CMD_STRING];
#endif /* (CFG_USB_INTERFACE_ENABLE != 0) */
char CommandString[C_SIZE_CMD_STRING];
static __IO uint16_t indexReceiveChar = 0;
static __IO uint16_t CptReceiveCmdFromUser = 0;

static TL_CmdPacket_t *p_thread_otcmdbuffer;
static TL_EvtPacket_t *p_thread_notif_M0_to_M4;
static __IO uint32_t  CptReceiveMsgFromM0 = 0; /* Debug counter */

PLACE_IN_SECTION("MB_MEM1") ALIGN(4) static TL_TH_Config_t ThreadConfigBuffer;
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static TL_CmdPacket_t ThreadOtCmdBuffer;
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t ThreadNotifRspEvtBuffer[sizeof(TL_PacketHeader_t) + TL_EVT_HDR_SIZE + 255U];
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static TL_CmdPacket_t ThreadCliCmdBuffer;

static otCoapResource OT_RessourceDataTransfer = {C_RESSOURCE_DATA_TRANSFER, APP_THREAD_DummyReqHandler, (void*)APP_THREAD_CoapDataReqHandler, NULL};
static otCoapResource OT_RessourceProvisionning = {C_RESSOURCE_Provisioning, APP_THREAD_DummyReqHandler, (void*)APP_THREAD_ProvisioningReqHandler, NULL};
static otMessageInfo OT_MessageInfo = {0};
static otCoapHeader  OT_Header = {0};
static uint8_t OT_Command = 0;
static uint16_t OT_BufferIdRead = 1U;
static uint16_t OT_BufferIdSend = 1U;
static uint8_t OT_BufferSend[COAP_PAYLOAD_MAX_LENGTH] = {0};
static uint8_t OT_BufferReceived[COAP_PAYLOAD_MAX_LENGTH] = {0};
static otMessage   * pOT_Message = NULL;
static otIp6Address   OT_PeerAddress = { .mFields.m8 = { 0 } };

/* Functions Definition ------------------------------------------------------*/
/**
 * @brief  Main entry point for the Thread Application
 * @param  none
 * @retval None
 */
void APP_THREAD_Init( void )
{
  SHCI_CmdStatus_t ThreadInitStatus;

  /* Check the compatibility with the Coprocessor Wireless Firmware loaded */
  APP_THREAD_CheckWirelessFirmwareInfo();

#if (CFG_USB_INTERFACE_ENABLE != 0)
  VCP_Init(&VcpTxBuffer[0], &VcpRxBuffer[0]);
#endif /* (CFG_USB_INTERFACE_ENABLE != 0) */
  /* Register cmdbuffer */
  APP_THREAD_RegisterCmdBuffer(&ThreadOtCmdBuffer);

  /* Init config buffer and call TL_THREAD_Init */
  APP_THREAD_TL_THREAD_INIT();

  /* Configure UART for sending CLI command from M4 */
  APP_THREAD_Init_UART_CLI();

  /* Send Thread start system cmd to M0 */
  ThreadInitStatus = SHCI_C2_THREAD_Init();
  /* Prevent unused argument(s) compilation warning */
  UNUSED(ThreadInitStatus);
  /* Register task */
  /* Create the different tasks */
  SCH_RegTask((uint32_t)CFG_TASK_MSG_FROM_M0_TO_M4, APP_THREAD_ProcessMsgM0ToM4);
  SCH_RegTask((uint32_t)CFG_TASK_SEND_BUFFER, APP_THREAD_SendNextBuffer);
  SCH_RegTask((uint32_t)CFG_TASK_PROVISIONING, APP_THREAD_AskProvisioning);

  /* Initialize and configure the Thread device*/
  APP_THREAD_DeviceConfig();
}

/**
 * @brief  Trace the error or the warning reported.
 * @param  ErrId :
 * @param  ErrCode
 * @retval None
 */
void APP_THREAD_Error(uint32_t ErrId, uint32_t ErrCode)
{
  switch(ErrId)
  {
  case ERR_REC_MULTI_MSG_FROM_M0 :
    APP_THREAD_TraceError("ERR:ERR_REC_MULTI_MSG_FROM_M0 ", ErrCode);
    break;
  case ERR_REC_MULTI_TRACE_FROM_M0 :
    APP_THREAD_TraceError("ERR:ERR_REC_MULTI_TRACE_FROM_M0 ", ErrCode);
    break;
  case ERR_THREAD_SET_STATE_CB :
    APP_THREAD_TraceError("ERR:ERR_THREAD_SET_STATE_CB ", ErrCode);
    break;
  case ERR_THREAD_SET_CHANNEL :
    APP_THREAD_TraceError("ERR:ERR_THREAD_SET_CHANNEL ", ErrCode);
    break;
  case ERR_THREAD_SET_PANID :
    APP_THREAD_TraceError("ERR:ERR_THREAD_SET_PANID ", ErrCode);
    break;
  case ERR_THREAD_IPV6_ENABLE :
    APP_THREAD_TraceError("ERR:ERR_THREAD_IPV6_ENABLE ", ErrCode);
    break;
  case ERR_THREAD_START :
    APP_THREAD_TraceError("ERR:ERR_THREAD_START ", ErrCode);
    break;
  case ERR_THREAD_COAP_START :
    APP_THREAD_TraceError("ERR:ERR_THREAD_COAP_START ", ErrCode);
    break;
  case ERR_THREAD_COAP_ADD_RESSOURCE :
    APP_THREAD_TraceError("ERR:ERR_THREAD_COAP_ADD_RESSOURCE ", ErrCode);
    break;
  case ERR_THREAD_MESSAGE_READ :
    APP_THREAD_TraceError("ERR:ERR_THREAD_MESSAGE_READ ", ErrCode);
    break;
  case ERR_THREAD_COAP_SEND_RESPONSE :
    APP_THREAD_TraceError("ERR:ERR_THREAD_COAP_SEND_RESPONSE ", ErrCode);
    break;
  case ERR_THREAD_COAP_APPEND :
    APP_THREAD_TraceError("ERR:ERR_THREAD_COAP_APPEND ", ErrCode);
    break;
  case ERR_THREAD_COAP_SEND_REQUEST :
    APP_THREAD_TraceError("ERR:ERR_THREAD_COAP_SEND_REQUEST ", ErrCode);
    break;
  case ERR_THREAD_SETUP :
    APP_THREAD_TraceError("ERR:ERR_THREAD_SETUP ", ErrCode);
    break;
  case ERR_THREAD_LINK_MODE :
    APP_THREAD_TraceError("ERR:ERR_THREAD_LINK_MODE ", ErrCode);
    break;
  case ERR_ALLOC_MSG :
    APP_THREAD_TraceError("ERR:ERR_ALLOC_MSG ", ErrCode);
    break;
  case ERR_FILE_RESP_HANDLER :
    APP_THREAD_TraceError("ERR:ERR_FILE_RESP_HANDLER ", ErrCode);
    break;
  case ERR_MSG_COMPARE_FAILED :
    APP_THREAD_TraceError("ERR:ERR_MSG_COMPARE_FAILED ", ErrCode);
    break;
  case ERR_NEW_MSG_ALLOC:
    APP_THREAD_TraceError("ERR:ERR_NEW_MSG_ALLOC ", ErrCode);
    break;
  case ERR_PROVISIONING_RESP:
    APP_THREAD_TraceError("ERR:ERR_PROVISIONING_RESP ", ErrCode);
    break;
  case ERR_THREAD_DATA_RESPONSE:
    APP_THREAD_TraceError("ERR:ERR_THREAD_DATA_RESPONSE ", ErrCode);
    break;
  case ERR_APPEND:
    APP_THREAD_TraceError("ERR:ERR_APPEND ", ErrCode);
    break;
  case ERR_HEADER_INIT:
    APP_THREAD_TraceError("ERR:ERR_HEADER_INIT ", ErrCode);
    break;
  case ERR_TOKEN:
    APP_THREAD_TraceError("ERR:ERR_TOKEN ", ErrCode);
    break;
  case ERR_THREAD_ERASE_PERSISTENT_INFO:
    APP_THREAD_TraceError("ERR:ERR_THREAD_ERASE_PERSISTENT_INFO ", ErrCode);
    break;
  case ERR_THREAD_CHECK_WIRELESS :
    APP_THREAD_TraceError("ERROR : ERR_THREAD_CHECK_WIRELESS ",ErrCode);
    break;
  default :
    APP_DBG("ERROR FATAL = %d\n", ErrId);
    APP_THREAD_TraceError("ERROR Unknown\n",0);
    break;
  }
}


/*************************************************************
 *
 * LOCAL FUNCTIONS
 *
 *************************************************************/

/**
 * @brief Thread initialization.
 *    This function configure the Thread mesh network.
 * @param  None
 * @retval None
 */
static void APP_THREAD_DeviceConfig(void)
{
  otError error;

  /* Configure the standard values */

  error = otInstanceErasePersistentInfo(NULL);
  if (error != OT_ERROR_NONE)
  {
    APP_THREAD_Error(ERR_THREAD_ERASE_PERSISTENT_INFO,error);
  }
  otInstanceFinalize(NULL);
  otInstanceInitSingle();
  error = otSetStateChangedCallback(NULL, APP_THREAD_StateNotif, NULL);
  if (error != OT_ERROR_NONE)
  {
    APP_THREAD_Error(ERR_THREAD_SET_STATE_CB,error);
  }
  error = otLinkSetChannel(NULL, C_CHANNEL_NB);
  if (error != OT_ERROR_NONE)
  {
    APP_THREAD_Error(ERR_THREAD_SET_CHANNEL,error);
  }
  error = otLinkSetPanId(NULL, C_PANID);
  if (error != OT_ERROR_NONE)
  {
    APP_THREAD_Error(ERR_THREAD_SET_PANID,error);
  }
  error = otIp6SetEnabled(NULL, true);
  if (error != OT_ERROR_NONE)
  {
    APP_THREAD_Error(ERR_THREAD_IPV6_ENABLE,error);
  }
  /* Start the COAP server */
  error = otCoapStart(NULL, OT_DEFAULT_COAP_PORT);
  if (error != OT_ERROR_NONE)
  {
    APP_THREAD_Error(ERR_THREAD_COAP_START,error);
  }
  /* Add COAP resources */
  error = otCoapAddResource(NULL, &OT_RessourceDataTransfer);
  if (error != OT_ERROR_NONE)
  {
    APP_THREAD_Error(ERR_THREAD_COAP_ADD_RESSOURCE,error);
  }
  /* Add APP_THREAD_AskProvisioning resources */
  error = otCoapAddResource(NULL, &OT_RessourceProvisionning);
  if (error != OT_ERROR_NONE)
  {
    APP_THREAD_Error(ERR_THREAD_COAP_ADD_RESSOURCE,error);
  }
  error = otThreadSetEnabled(NULL, true);
  if (error != OT_ERROR_NONE)
  {
    APP_THREAD_Error(ERR_THREAD_START,error);
  }
}


/**
 * @brief Thread notification when the state changes.
 *    When the Thread device change state, a specific LED
 *    color is being displayed.
 *    LED2 On (Green) means that the device is in "Leader" mode.
 *    LED3 On (Red) means that the device is in "Child: mode or
 *       in "Router" mode.
 *    LED2 and LED3 off means that the device is in "Disabled"
 *       or "Detached" mode.
 * @param  aFlags  : Define the item that has been modified
 *     aContext: Context
 *
 * @retval None
 */
static void APP_THREAD_StateNotif(uint32_t NotifFlags, void *pContext)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(pContext);

  static uint32_t provisioning = 0;

  if ((NotifFlags & (uint32_t)OT_CHANGED_THREAD_ROLE) == (uint32_t)OT_CHANGED_THREAD_ROLE)
  {
    switch (otThreadGetDeviceRole(NULL))
    {
    case OT_DEVICE_ROLE_DISABLED:
      BSP_LED_Off(LED2);
      BSP_LED_Off(LED3);
      break;
    case OT_DEVICE_ROLE_DETACHED:
      BSP_LED_Off(LED2);
      BSP_LED_Off(LED3);
      break;
    case OT_DEVICE_ROLE_CHILD:
      BSP_LED_Off(LED2);
      BSP_LED_On(LED3);
      if (provisioning == 0)
      {
        HAL_Delay(3000U);
        SCH_SetTask(TASK_PROVISIONING, CFG_SCH_PRIO_1);
      }
      provisioning = 1U;
      break;
    case OT_DEVICE_ROLE_ROUTER :
      BSP_LED_Off(LED2);
      BSP_LED_On(LED3);
      break;
    case OT_DEVICE_ROLE_LEADER :
      BSP_LED_On(LED2);
      BSP_LED_Off(LED3);
      break;
    default:
      BSP_LED_Off(LED2);
      BSP_LED_Off(LED3);
      break;
    }
  }
}

/**
 * @brief Dummy request handler
 *
 * @param None
 * @retval None
 */
static void APP_THREAD_DummyReqHandler(void        * p_context,
    otCoapHeader    * pHeader,
    otMessage       * pMessage,
    const otMessageInfo * pMessageInfo)
{
}

/**
 * @brief  Warn the user that an error has occurred.In this case,
 *     the LEDs on the Board will start blinking.
 *
 * @param  Mess  : Message associated to the error.
 * @param  ErrCode: Error code associated to the module (OpenThread or other module if any)
 * @retval None
 */
static void APP_THREAD_TraceError(const char * pMess, uint32_t ErrCode)
{
  APP_DBG("**** Fatal error = %s (Err = %d)",pMess,ErrCode);
  while(1U == 1U)
  {
    BSP_LED_Toggle(LED1);
    HAL_Delay(500U);
    BSP_LED_Toggle(LED2);
    HAL_Delay(500U);
    BSP_LED_Toggle(LED3);
    HAL_Delay(500U);
  }
}

/**
 * @brief  This function is used to compare the message received versus
 *     the original message.
 * @param  None
 * @retval None
 */
static void APP_THREAD_CheckMsgValidity(void)
{
  uint16_t mMsgId=OT_BufferIdRead - 1U; /* Corrected MsgId (MsgId starts at 1, not at 0) */
  uint32_t mOffset = COAP_PAYLOAD_MAX_LENGTH*mMsgId; /* the starting offset */
  uint32_t i;
  bool valid =true;

  for(i = mOffset; i < mOffset+COAP_PAYLOAD_MAX_LENGTH; i++)
  {
    if(OT_BufferReceived[i-mOffset] != aDataBuffer[i])
    {
      valid =false;
    }
  }

  if(valid == false)
    APP_THREAD_Error(ERR_MSG_COMPARE_FAILED, 0);
}

/**
 * @brief  This function compute the next message to be send
 * @param  None
 * @retval None
 */
static void APP_THREAD_SendNextBuffer(void)
{
  uint16_t j;
  uint16_t mOffset;

  if (OT_BufferIdSend < 5U)
  {
    /* Prepare next buffers to be send */
    OT_BufferIdSend++;
    mOffset=(OT_BufferIdSend - 1U) * COAP_PAYLOAD_MAX_LENGTH;

    memset(OT_BufferSend, 0, COAP_PAYLOAD_MAX_LENGTH);
    for(j = mOffset; j < mOffset + COAP_PAYLOAD_MAX_LENGTH; j++)
    {
      OT_BufferSend[j - mOffset] = aDataBuffer[j];
    }

    /* Send the data in unicast mode */
    APP_THREAD_SendCoapUnicastRequest();
  }
  else
  {
    /* Buffer transfer has been successfully  transfered */
    BSP_LED_On(LED1);
    APP_DBG(" ********* BUFFER HAS BEEN TRANFERED \r\n");
  }
}

/**
 * @brief Data request handler triggered at the reception of the COAP message
 * @param pHeader header pointer
 * @param pMessage message pointer
 * @param pMessageInfo message info pointer
 * @retval None
 */
static void APP_THREAD_CoapDataReqHandler(otCoapHeader    * pHeader,
    otMessage       * pMessage,
    const otMessageInfo * pMessageInfo)
{
  do
  {
    APP_DBG(" ********* APP_THREAD_CoapDataReqHandler \r\n");
    if (otCoapHeaderGetType(pHeader) != OT_COAP_TYPE_CONFIRMABLE &&
        otCoapHeaderGetType(pHeader) != OT_COAP_TYPE_NON_CONFIRMABLE)
    {
      APP_THREAD_Error(ERR_THREAD_COAP_ADD_RESSOURCE, 0);
      break;
    }

    if (otCoapHeaderGetCode(pHeader) != OT_COAP_CODE_PUT)
    {
      APP_THREAD_Error(ERR_THREAD_COAP_ADD_RESSOURCE, 0);
      break;
    }

    if (otMessageRead(pMessage, otMessageGetOffset(pMessage), &OT_BufferReceived, sizeof(OT_BufferReceived)) != sizeof(OT_BufferReceived))
    {
      APP_THREAD_Error(ERR_THREAD_MESSAGE_READ, 0);
    }
    OT_BufferIdRead=otCoapHeaderGetMessageId(pHeader);

    APP_THREAD_CheckMsgValidity();

    if (otCoapHeaderGetType(pHeader) == OT_COAP_TYPE_CONFIRMABLE)
    {
      APP_THREAD_SendDataResponse(pHeader, pMessageInfo);
    }
  } while (false);
}

/**
 * @brief This function acknowledge the data reception by sending an ACK
 *    back to the sender.
 * @param  pRequestHeader coap header
 * @param  pMessageInfo message info pointer
 * @retval None
 */
static void APP_THREAD_SendDataResponse(otCoapHeader    * pRequestHeader,
    const otMessageInfo * pMessageInfo)
{
  otError  error = OT_ERROR_NONE;

  APP_DBG(" ********* APP_THREAD_SendDataResponse \r\n");
  otCoapHeaderInit(&OT_Header, OT_COAP_TYPE_ACKNOWLEDGMENT, OT_COAP_CODE_CHANGED);
  otCoapHeaderSetMessageId(&OT_Header, otCoapHeaderGetMessageId(pRequestHeader));
  otCoapHeaderSetToken(&OT_Header,
      otCoapHeaderGetToken(pRequestHeader),
      otCoapHeaderGetTokenLength(pRequestHeader));

  pOT_Message = otCoapNewMessage(NULL, &OT_Header);
  if (pOT_Message == NULL)
  {
    APP_THREAD_Error(ERR_NEW_MSG_ALLOC,error);
  }
  error = otCoapSendResponse(NULL, pOT_Message, pMessageInfo);
  if (error != OT_ERROR_NONE && pOT_Message != NULL)
  {
    otMessageFree(pOT_Message);
    APP_THREAD_Error(ERR_THREAD_DATA_RESPONSE,error);
  }
}



/**
 * @brief This function is used to handle the APP_THREAD_AskProvisioning handler
 *
 * @param pHeader header pointer
 * @param pMessage message pointer
 * @param pMessageInfo message info pointer
 * @retval None
 */
static void APP_THREAD_ProvisioningReqHandler(otCoapHeader    * pHeader,
    otMessage       * pMessage,
    const otMessageInfo * pMessageInfo)
{
  (void)pMessage;

  APP_DBG("**** Leader receives APP_THREAD_AskProvisioning_request *****\n\r");
  if (otCoapHeaderGetType(pHeader) == OT_COAP_TYPE_NON_CONFIRMABLE &&
      otCoapHeaderGetCode(pHeader) == OT_COAP_CODE_GET)
  {
    OT_MessageInfo = *pMessageInfo;
    memset(&OT_MessageInfo.mSockAddr, 0, sizeof(OT_MessageInfo.mSockAddr));
    if (APP_THREAD_ProvisioningRespSend(pHeader, &OT_MessageInfo) != OT_ERROR_NONE)
    {
      APP_THREAD_Error(ERR_PROVISIONING_RESP, 0);
    }
  }
}

/**
 * @brief This function is used to handle the APP_THREAD_AskProvisioning response
 *
 * @param pRequestHeader
 * @param pMessageInfo message info pointer
 * @retval error code
 */
static otError APP_THREAD_ProvisioningRespSend(otCoapHeader    * pRequestHeader,
    const otMessageInfo * pMessageInfo)
{
  otError  error = OT_ERROR_NO_BUFS;

  do
  {
    otCoapHeaderInit(&OT_Header, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_CONTENT);
    otCoapHeaderSetToken(&OT_Header,
        otCoapHeaderGetToken(pRequestHeader),
        otCoapHeaderGetTokenLength(pRequestHeader));
    otCoapHeaderSetPayloadMarker(&OT_Header);

    pOT_Message = otCoapNewMessage(NULL, &OT_Header);
    if (pOT_Message == NULL)
    {
      APP_THREAD_Error(ERR_ALLOC_MSG, error);
    }

    error = otMessageAppend(pOT_Message, &OT_Command, sizeof(OT_Command));
    if (error != OT_ERROR_NONE)
    {
      APP_THREAD_Error(ERR_APPEND, error);
    }

    error = otMessageAppend(pOT_Message, otThreadGetMeshLocalEid(NULL), sizeof(otIp6Address));
    if (error != OT_ERROR_NONE)
    {
      break;
    }
    APP_DBG("**** 2) APP_THREAD_ProvisioningRespSend *****\n\r");
    error = otCoapSendResponse(NULL, pOT_Message, pMessageInfo);

    if (error != OT_ERROR_NONE && pOT_Message != NULL)
    {
      otMessageFree(pOT_Message);
      APP_THREAD_Error(ERR_THREAD_COAP_SEND_RESP, error);
    }
  } while (false);

  return error;
}

/**
 * @brief This function is used to manage the APP_THREAD_AskProvisioning request
 *
 * @param None
 * @retval None
 */
static void APP_THREAD_ProvisioningReqSend()
{
  otError   error = OT_ERROR_NONE;

  do
  {
    otCoapHeaderInit(&OT_Header, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_GET);

    otCoapHeaderGenerateToken(&OT_Header, 2U);

    error = otCoapHeaderAppendUriPathOptions(&OT_Header, C_RESSOURCE_Provisioning);
    if (error != OT_ERROR_NONE)
    {
      APP_THREAD_Error(ERR_APEND_URI,error);
    }

    pOT_Message = otCoapNewMessage(NULL, &OT_Header);
    if (pOT_Message == NULL)
    {
      APP_THREAD_Error(ERR_NEW_MSG_ALLOC,error);
    }

    memset(&OT_MessageInfo, 0, sizeof(OT_MessageInfo));
    OT_MessageInfo.mInterfaceId = OT_NETIF_INTERFACE_ID_THREAD;
    OT_MessageInfo.mPeerPort = OT_DEFAULT_COAP_PORT;
    otIp6AddressFromString("FF03::1", &OT_MessageInfo.mPeerAddr);

    APP_DBG("**** 1) APP_THREAD_ProvisioningReqSend *****\n\r");
    error = otCoapSendRequest(NULL,
        pOT_Message,
        &OT_MessageInfo,
        &APP_THREAD_DummyRespHandler,
        (void*)&APP_THREAD_ProvisioningRespHandler);
  } while (false);

  if (error != OT_ERROR_NONE && pOT_Message != NULL)
  {
    otMessageFree(pOT_Message);
  }
}

/**
 * @brief This function is used to manage the APP_THREAD_AskProvisioning response
 *    handler.
 *
 * @param pHeader  header
 * @param pMessage message pointer
 * @param pMessageInfo message info pointer
 * @param Result error code if any
 * @retval None
 */
static void APP_THREAD_ProvisioningRespHandler(otCoapHeader        * pHeader,
    otMessage           * pMessage,
    const otMessageInfo * pMessageInfo,
    otError             Result)
{
  (void)pHeader;
  if (Result == OT_ERROR_NONE)
  {
    if ((otMessageRead(pMessage, otMessageGetOffset(pMessage), &OT_Command, sizeof(OT_Command)) == sizeof(OT_Command)))
    {
      /* Retrieve the */
      if (otMessageRead(pMessage,
          otMessageGetOffset(pMessage) + sizeof(OT_Command),
          &OT_PeerAddress,
          sizeof(OT_PeerAddress)) != sizeof(OT_PeerAddress))
      {
        APP_THREAD_Error(ERR_READ, 0);
      }
      APP_DBG("**** 3) APP_THREAD_ProvisioningRespHandler *****");
      /* Ask to start the first transfer */
      SCH_SetTask(TASK_SEND_BUFFER, CFG_SCH_PRIO_1);
    }
  }
  else
  {
    APP_DBG("**** 3)APP_THREAD_AskProvisioning failed ***** with ERROR code = %d\n\r",Result);
  }
}


/**
 * @brief This function initiates the APP_THREAD_AskProvisioning phase
 *
 * @param None
 * @retval None
 */
static void APP_THREAD_AskProvisioning(void)
{
  HAL_Delay(1000U);
  APP_THREAD_ProvisioningReqSend();
}
/**
 * @brief This function is used to send the data via a Coap message in
 *    confirmable mode.
 *
 * @param None
 * @retval None
 */
static void APP_THREAD_SendCoapUnicastRequest()
{
  otError   error = OT_ERROR_NONE;

  otCoapHeaderInit(&OT_Header, OT_COAP_TYPE_CONFIRMABLE, OT_COAP_CODE_PUT);
  otCoapHeaderSetMessageId(&OT_Header,OT_BufferIdSend);
  otCoapHeaderGenerateToken(&OT_Header, 2U);

  error = otCoapHeaderAppendUriPathOptions(&OT_Header,C_RESSOURCE_DATA_TRANSFER);
  if (error != OT_ERROR_NONE)
  {
    APP_THREAD_Error(ERR_APEND_URI,error);
  }
  otCoapHeaderSetPayloadMarker(&OT_Header);
  pOT_Message = otCoapNewMessage(NULL, &OT_Header);
  if (pOT_Message == NULL)
  {
    APP_THREAD_Error(ERR_ALLOC_MSG,error);
  }
  error = otMessageAppend(pOT_Message, &OT_BufferSend, sizeof(OT_BufferSend));
  if (error != OT_ERROR_NONE)
  {
    APP_THREAD_Error(ERR_THREAD_COAP_APPEND,error);
  }
  memset(&OT_MessageInfo, 0, sizeof(OT_MessageInfo));
  OT_MessageInfo.mInterfaceId = OT_NETIF_INTERFACE_ID_THREAD;
  OT_MessageInfo.mPeerPort = OT_DEFAULT_COAP_PORT;
  memcpy(&OT_MessageInfo.mPeerAddr, &OT_PeerAddress, sizeof(OT_MessageInfo.mPeerAddr));
  error = otCoapSendRequest(NULL,
      pOT_Message,
      &OT_MessageInfo,
      &APP_THREAD_DummyRespHandler,
      (void*)&APP_THREAD_DataRespHandler);

  if (error != OT_ERROR_NONE && pOT_Message != NULL)
  {
    otMessageFree(pOT_Message);
  }
}

/**
 * @brief This function manages the data response handler
 *    and reschedules the sending of data.
 *
 * @param pHeader  header
 * @param pMessage message pointer
 * @param pMessageInfo message info pointer
 * @param Result error code
 * @retval None
 */
static void APP_THREAD_DataRespHandler(otCoapHeader        * pHeader,
    otMessage           * pMessage,
    const otMessageInfo * pMessageInfo,
    otError             Result)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(pHeader);
  UNUSED(pMessage);
  UNUSED(pMessageInfo);

  if (Result == OT_ERROR_NONE)
  {
    /* Ask to perform a new transfer */
    HAL_Delay(1000U);
    SCH_SetTask(TASK_SEND_BUFFER, CFG_SCH_PRIO_1);
  }
  else
  {
    APP_THREAD_Error(ERR_FILE_RESP_HANDLER,Result);
  }
}

/**
 * @brief This function is used to handle a dummy response handler
 *
 * @param p_context  context
 * @param pHeader  coap header
 * @param pMessage message
 * @paramp pMessageInfo otMessage information
 * @param Result error status
 * @retval None
 */
static void APP_THREAD_DummyRespHandler(void                * p_context,
    otCoapHeader        * pHeader,
    otMessage           * pMessage,
    const otMessageInfo * pMessageInfo,
    otError             Result)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(p_context);
  UNUSED(pHeader);
  UNUSED(pMessage);
  UNUSED(pMessageInfo);
  UNUSED(Result);
}

/**
 * @brief Check if the Coprocessor Wireless Firmware loaded supports Thread
 *        and display associated informations
 * @param  None
 * @retval None
 */
static void APP_THREAD_CheckWirelessFirmwareInfo(void)
{
  WirelessFwInfo_t wireless_info_instance;
  WirelessFwInfo_t* p_wireless_info = &wireless_info_instance;

  if (SHCI_GetWirelessFwInfo(p_wireless_info) != SHCI_Success)
  {
    APP_THREAD_Error((uint32_t)ERR_THREAD_CHECK_WIRELESS, (uint32_t)ERR_INTERFACE_FATAL);
  }
  else
  {
    APP_DBG("**********************************************************");
    APP_DBG("WIRELESS COPROCESSOR FW:");
    /* Print version */
    APP_DBG("VERSION ID = %d.%d.%d", p_wireless_info->VersionMajor, p_wireless_info->VersionMinor, p_wireless_info->VersionSub);

    switch(p_wireless_info->StackType)
    {
    case INFO_STACK_TYPE_THREAD_FTD :
      APP_DBG("FW Type : Thread FTD");
      break;
    case INFO_STACK_TYPE_THREAD_MTD :
      APP_DBG("FW Type : Thread MTD");
      break;
    case INFO_STACK_TYPE_BLE_THREAD_FTD_STATIC :
      APP_DBG("FW Type : Static Concurrent Mode BLE/Thread");
      break;
    default :
      /* No Thread device supported ! */
      APP_THREAD_Error((uint32_t)ERR_THREAD_CHECK_WIRELESS, (uint32_t)ERR_INTERFACE_FATAL);
      break;
    }
    APP_DBG("**********************************************************");
  }
}
/*************************************************************
 *
 * WRAP FUNCTIONS
 *
 *************************************************************/

void APP_THREAD_RegisterCmdBuffer(TL_CmdPacket_t* p_buffer)
{
  p_thread_otcmdbuffer = p_buffer;
}


Thread_OT_Cmd_Request_t* THREAD_Get_OTCmdPayloadBuffer(void)
{
  return (Thread_OT_Cmd_Request_t*)p_thread_otcmdbuffer->cmdserial.cmd.payload;
}

Thread_OT_Cmd_Request_t* THREAD_Get_OTCmdRspPayloadBuffer(void)
{
  return (Thread_OT_Cmd_Request_t*)((TL_EvtPacket_t *)p_thread_otcmdbuffer)->evtserial.evt.payload;
}

Thread_OT_Cmd_Request_t* THREAD_Get_NotificationPayloadBuffer(void)
{
  return (Thread_OT_Cmd_Request_t*)(p_thread_notif_M0_to_M4)->evtserial.evt.payload;
}

/**
 * @brief  This function is used to transfer the Ot commands from the
 *         M4 to the M0.
 *
 * @param   None
 * @return  None
 */
void Ot_Cmd_Transfer(void)
{
  /* OpenThread OT command cmdcode range 0x280 .. 0x3DF = 352 */
  p_thread_otcmdbuffer->cmdserial.cmd.cmdcode = 0x280U;
  /* Size = otCmdBuffer->Size (Number of OT cmd arguments : 1 arg = 32bits so multiply by 4 to get size in bytes)
   * + ID (4 bytes) + Size (4 bytes) */
  uint32_t l_size = ((Thread_OT_Cmd_Request_t*)(p_thread_otcmdbuffer->cmdserial.cmd.payload))->Size * 4U + 8U;
  p_thread_otcmdbuffer->cmdserial.cmd.plen = l_size;

  TL_OT_SendCmd();

  /* Wait completion of cmd */
  Wait_Getting_Ack_From_M0();
}

/**
 * @brief  This function is called when acknowledge from OT command is received from the M0+.
 *
 * @param   Otbuffer : a pointer to TL_EvtPacket_t
 * @return  None
 */
void TL_OT_CmdEvtReceived( TL_EvtPacket_t * Otbuffer )
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(Otbuffer);

  Receive_Ack_From_M0();
}

/**
 * @brief  This function is called when notification from M0+ is received.
 *
 * @param   Notbuffer : a pointer to TL_EvtPacket_t
 * @return  None
 */
void TL_THREAD_NotReceived( TL_EvtPacket_t * Notbuffer )
{
  p_thread_notif_M0_to_M4 = Notbuffer;

  Receive_Notification_From_M0();
}

/**
 * @brief  This function is called before sending any ot command to the M0
 *         core. The purpose of this function is to be able to check if
 *         there are no notifications coming from the M0 core which are
 *         pending before sending a new ot command.
 * @param  None
 * @retval None
 */
void Pre_OtCmdProcessing(void)
{
  SCH_WaitEvt( EVENT_SYNCHRO_BYPASS_IDLE);
}

/**
 * @brief  This function waits for getting an acknowledgment from the M0.
 *
 * @param  None
 * @retval None
 */
static void Wait_Getting_Ack_From_M0(void)
{
  SCH_WaitEvt(EVENT_ACK_FROM_M0_EVT);
}

/**
 * @brief  Receive an acknowledgment from the M0+ core.
 *         Each command send by the M4 to the M0 are acknowledged.
 *         This function is called under interrupt.
 * @param  None
 * @retval None
 */
static void Receive_Ack_From_M0(void)
{
  SCH_SetEvt(EVENT_ACK_FROM_M0_EVT);
}

/**
 * @brief  Receive a notification from the M0+ through the IPCC.
 *         This function is called under interrupt.
 * @param  None
 * @retval None
 */
static void Receive_Notification_From_M0(void)
{
  CptReceiveMsgFromM0++;
  SCH_SetTask(TASK_MSG_FROM_M0_TO_M4,CFG_SCH_PRIO_0);
}

#if (CFG_USB_INTERFACE_ENABLE != 0)
#else
static void RxCpltCallback(void)
{
  /* Filling buffer and wait for '\r' char */
  if (indexReceiveChar < C_SIZE_CMD_STRING)
  {
    CommandString[indexReceiveChar++] = aRxBuffer[0];
    if (aRxBuffer[0] == '\r')
    {
      CptReceiveCmdFromUser = 1U;

      /* UART task scheduling*/
      SCH_SetTask(1U << CFG_TASK_SEND_CLI_TO_M0, CFG_SCH_PRIO_0);
    }
  }

  /* Once a character has been sent, put back the device in reception mode */
  HW_UART_Receive_IT(UART_CLI, aRxBuffer, 1U, RxCpltCallback);
}
#endif /* (CFG_USB_INTERFACE_ENABLE != 0) */

#if (CFG_USB_INTERFACE_ENABLE != 0)
/**
 * @brief Process the command strings.
 *        As soon as a complete command string has been received, the task
 *        in charge of sending the command to the M0 is scheduled
 * @param  None
 * @retval None
 */
static uint32_t  ProcessCmdString( uint8_t* buf , uint32_t len )
{
  uint32_t i,j,tmp_start;
  tmp_start = 0;
  uint32_t res = 0;

  i= 0;
  while ((buf[i] != '\r') && (i < len))
  {
    i++;
  }

  if (i != len)
  {
    memcpy(CommandString, buf,(i+1));
    indexReceiveChar = i + 1U; /* Length of the buffer containing the command string */

    SCH_SetTask(1U << CFG_TASK_SEND_CLI_TO_M0, CFG_SCH_PRIO_0);

    tmp_start = i;
    for (j = 0; j < (len - tmp_start - 1U) ; j++)
    {
      buf[j] = buf[tmp_start + j + 1U];
    }
    res = len - tmp_start - 1U;
  }
  else
  {
    res = len;
  }
  return res; /* Remaining characters in the temporary buffer */
}
#endif/* (CFG_USB_INTERFACE_ENABLE != 0) */

/**
 * @brief Process sends receive CLI command to M0.
 * @param  None
 * @retval None
 */
static void Send_CLI_To_M0(void)
{
  memset(ThreadCliCmdBuffer.cmdserial.cmd.payload, 0x0U, 255U);
  memcpy(ThreadCliCmdBuffer.cmdserial.cmd.payload, CommandString, indexReceiveChar);
  ThreadCliCmdBuffer.cmdserial.cmd.plen = indexReceiveChar;
  ThreadCliCmdBuffer.cmdserial.cmd.cmdcode = 0x0;

  /* Clear receive buffer, character counter and command complete */
  CptReceiveCmdFromUser = 0;
  indexReceiveChar = 0;
  memset(CommandString, 0, C_SIZE_CMD_STRING);

  TL_CLI_SendCmd();
}

/**
 * @brief Send notification for CLI TL Channel.
 * @param  None
 * @retval None
 */
static void Send_CLI_Ack_For_OT(void)
{

  /* Notify M0 that characters have been sent to UART */
  TL_THREAD_CliSendAck();
}

/**
 * @brief Perform initialization of CLI UART interface.
 * @param  None
 * @retval None
 */
void APP_THREAD_Init_UART_CLI(void)
{
  SCH_RegTask(CFG_TASK_SEND_CLI_TO_M0,Send_CLI_To_M0);
#if (CFG_USB_INTERFACE_ENABLE != 0)
#else
  HW_UART_Init(UART_CLI);
  HW_UART_Receive_IT(UART_CLI, aRxBuffer, 1, RxCpltCallback);
#endif /* (CFG_USB_INTERFACE_ENABLE != 0) */
}

/**
 * @brief Perform initialization of TL for THREAD.
 * @param  None
 * @retval None
 */
void APP_THREAD_TL_THREAD_INIT(void)
{
  ThreadConfigBuffer.p_ThreadOtCmdRspBuffer = (uint8_t*)&ThreadOtCmdBuffer;
  ThreadConfigBuffer.p_ThreadNotAckBuffer = (uint8_t*)ThreadNotifRspEvtBuffer;
  ThreadConfigBuffer.p_ThreadCliRspBuffer = (uint8_t*)&ThreadCliCmdBuffer;

  TL_THREAD_Init( &ThreadConfigBuffer );
}

/**
 * @brief  This function is called when notification on CLI TL Channel from M0+ is received.
 *
 * @param   Notbuffer : a pointer to TL_EvtPacket_t
 * @return  None
 */
void TL_THREAD_CliNotReceived( TL_EvtPacket_t * Notbuffer )
{
  TL_CmdPacket_t* l_CliBuffer = (TL_CmdPacket_t*)Notbuffer;
  uint8_t l_size = l_CliBuffer->cmdserial.cmd.plen;

  /* WORKAROUND: if string to output is "> " then respond directly to M0 and do not output it */
  if (strcmp((const char *)l_CliBuffer->cmdserial.cmd.payload, "> ") != 0)
  {
    /* Write to CLI UART */
#if (CFG_USB_INTERFACE_ENABLE != 0)
    VCP_SendData( l_CliBuffer->cmdserial.cmd.payload, l_size, HostTxCb);
#else
    HW_UART_Transmit_IT(UART_CLI, l_CliBuffer->cmdserial.cmd.payload, l_size, HostTxCb);
#endif /*USAGE_OF_VCP */
  }
  else
  {
    Send_CLI_Ack_For_OT();
  }
}

/**
 * @brief  End of transfer callback for CLI UART sending.
 *
 * @param   Notbuffer : a pointer to TL_EvtPacket_t
 * @return  None
 */
void HostTxCb(void)
{
  Send_CLI_Ack_For_OT();
}

/**
 * @brief Process the messages coming from the M0.
 * @param  None
 * @retval None
 */
void APP_THREAD_ProcessMsgM0ToM4(void)
{
  if (CptReceiveMsgFromM0 != 0)
  {
    /* If CptReceiveMsgFromM0 is > 1. it means that we did not serve all the events from the radio */
    if (CptReceiveMsgFromM0 > 1U)
    {
      APP_THREAD_Error(ERR_REC_MULTI_MSG_FROM_M0, 0);
    }
    else
    {
      OpenThread_CallBack_Processing();
    }
    /* Reset counter */
    CptReceiveMsgFromM0 = 0;
  }
}

#if (CFG_USB_INTERFACE_ENABLE != 0)
/**
 * @brief  This function is called when thereare some data coming
 *         from the Hyperterminal via the USB port
 *         Data received over USB OUT endpoint are sent over CDC interface
 *         through this function.
 * @param  Buf: Buffer of data received
 * @param  Len: Number of data received (in bytes)
 * @retval Number of characters remaining in the buffer and not yet processed
 */
void VCP_DataReceived(uint8_t* Buf , uint32_t *Len)
{
  uint32_t i,flag_continue_checking = TRUE;
  uint32_t char_remaining = 0;
  static uint32_t len_total = 0;

  /* Copy the characteres in the temporary buffer */
  for (i = 0; i < *Len; i++)
  {
    TmpString[len_total++] = Buf[i];
  }

  /* Process the buffer commands one by one     */
  /* A command is limited by a \r caracaters    */
  while (flag_continue_checking == TRUE)
  {
    char_remaining = ProcessCmdString(TmpString,len_total);
    /* If char_remaining is equal to len_total, it means that the command string is not yet
     * completed.
     * If char_remaining is equal to 0, it means that the command string has
     * been entirely processed.
     */
    if ((char_remaining == 0) || (char_remaining == len_total))
    {
      flag_continue_checking = FALSE;
    }
    len_total = char_remaining;
  }
}
#endif /* (CFG_USB_INTERFACE_ENABLE != 0) */



/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

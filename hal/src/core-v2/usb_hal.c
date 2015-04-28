/**
 ******************************************************************************
 * @file    usb_hal.c
 * @author  Satish Nair
 * @version V1.0.0
 * @date    05-Nov-2014
 * @brief   USB Virtual COM Port and HID device HAL
 ******************************************************************************
  Copyright (c) 2013-14 Spark Labs, Inc.  All rights reserved.

  Copyright 2012 STMicroelectronics
  http://www.st.com/software_license_agreement_liberty_v2

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation, either
  version 3 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program; if not, see <http://www.gnu.org/licenses/>.
 ******************************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "usb_hal.h"
#include "usbd_cdc_core.h"
#include "usbd_usr.h"
#include "usb_conf.h"
#include "usbd_desc.h"
#include "delay_hal.h"
#include "interrupts_hal.h"

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
USB_OTG_CORE_HANDLE USB_OTG_dev;

extern uint32_t USBD_OTG_ISR_Handler(USB_OTG_CORE_HANDLE *pdev);
#ifdef USB_OTG_HS_DEDICATED_EP1_ENABLED
extern uint32_t USBD_OTG_EP1IN_ISR_Handler(USB_OTG_CORE_HANDLE *pdev);
extern uint32_t USBD_OTG_EP1OUT_ISR_Handler(USB_OTG_CORE_HANDLE *pdev);
#endif

/* Extern variables ----------------------------------------------------------*/
#ifdef USB_CDC_ENABLE
extern LINE_CODING linecoding;
extern volatile uint8_t USB_DEVICE_CONFIGURED;
extern volatile uint8_t USB_Rx_Buffer[];
extern volatile uint8_t APP_Rx_Buffer[];
extern volatile uint32_t APP_Rx_ptr_in;
extern volatile uint32_t APP_Rx_ptr_out;
extern volatile uint16_t USB_Rx_length;
extern volatile uint16_t USB_Rx_ptr;
extern volatile uint8_t  USB_Tx_State;
extern volatile uint8_t  USB_Rx_State;
#endif

#if defined (USB_CDC_ENABLE) || defined (USB_HID_ENABLE)
/*******************************************************************************
 * Function Name  : SPARK_USB_Setup
 * Description    : Spark USB Setup.
 * Input          : None.
 * Return         : None.
 *******************************************************************************/
void SPARK_USB_Setup(void)
{
    
    USBD_Init(&USB_OTG_dev,
#ifdef USE_USB_OTG_FS
            USB_OTG_FS_CORE_ID,
#elif defined USE_USB_OTG_HS
            USB_OTG_HS_CORE_ID,
#endif
            &USR_desc,
            &USBD_CDC_cb,
            NULL);
}

/*******************************************************************************
 * Function Name  : Get_SerialNum.
 * Description    : Create the serial number string descriptor.
 * Input          : None.
 * Output         : None.
 * Return         : None.
 *******************************************************************************/
void Get_SerialNum(void)
{
    //Not required. Retained for compatibility
}
#endif

#ifdef USB_CDC_ENABLE
/*******************************************************************************
 * Function Name  : USB_USART_Init
 * Description    : Start USB-USART protocol.
 * Input          : baudRate (0 : disconnect usb else init usb only once).
 * Return         : None.
 *******************************************************************************/
void USB_USART_Init(uint32_t baudRate)
{
    if (linecoding.bitrate != baudRate)
    {
        if (!baudRate)
        {
            USB_Cable_Config(DISABLE);
        }
        else if (!linecoding.bitrate)
        {
            //Perform a Detach-Attach operation on USB bus
            USB_Cable_Config(DISABLE);
            USB_Cable_Config(ENABLE);

            //Initialize USB device only once (if linecoding.bitrate==0)
            SPARK_USB_Setup();
        }
        //linecoding.bitrate will be overwritten by USB Host
        linecoding.bitrate = baudRate;
    }
}

unsigned USB_USART_Baud_Rate(void) 
{
    return linecoding.bitrate;
}

void USB_USART_LineCoding_BitRate_Handler(void (*handler)(uint32_t bitRate))
{
    //Init USB Serial first before calling the linecoding handler
    USB_USART_Init(9600);

    HAL_Delay_Milliseconds(1000);

    //Set the system defined custom handler
    SetLineCodingBitRateHandler(handler);
}

/*******************************************************************************
 * Function Name  : USB_USART_Available_Data.
 * Description    : Return the length of available data received from USB.
 * Input          : None.
 * Return         : Length.
 *******************************************************************************/
uint8_t USB_USART_Available_Data(void)
{
    if(USB_Rx_State == 1)
    {
        return (USB_Rx_length - USB_Rx_ptr);
    }

    return 0;
}

/*******************************************************************************
 * Function Name  : USB_USART_Receive_Data.
 * Description    : Return data sent by USB Host.
 * Input          : None
 * Return         : Data.
 *******************************************************************************/
int32_t USB_USART_Receive_Data(uint8_t peek)
{
    if(USB_Rx_State == 1)
    {
        int result = USB_Rx_Buffer[peek ? USB_Rx_ptr : USB_Rx_ptr++];
        if(!peek && ((USB_Rx_length - USB_Rx_ptr) == 0))
        {
            USB_Rx_State = 0;

            /* Prepare Out endpoint to receive next packet */
            DCD_EP_PrepareRx(&USB_OTG_dev,
                             CDC_OUT_EP,
                             (uint8_t*)(USB_Rx_Buffer),
                             CDC_DATA_OUT_PACKET_SIZE);
        }
        return result;
    }

    return -1;
}

void USB_OTG_BSP_EnableInterrupt(USB_OTG_CORE_HANDLE *pdev);

/*******************************************************************************
 * Function Name  : USB_USART_Send_Data.
 * Description    : Send Data from USB_USART to USB Host.
 * Input          : Data.
 * Return         : None.
 *******************************************************************************/
void USB_USART_Send_Data(uint8_t Data)
{        
    USB_OTG_BSP_EnableInterrupt(&USB_OTG_dev);
    int mask = HAL_disable_irq();

    int next = (APP_Rx_ptr_in+1) & (APP_RX_DATA_SIZE-1);
    
    while (next == APP_Rx_ptr_out) {
        HAL_enable_irq(mask);    
        USB_OTG_BSP_EnableInterrupt(&USB_OTG_dev);
        HAL_Delay_Milliseconds(1);
        mask = HAL_disable_irq();
    }

    APP_Rx_Buffer[APP_Rx_ptr_in] = Data;    
    APP_Rx_ptr_in = next;
    HAL_enable_irq(mask);
}
#endif

#ifdef USB_HID_ENABLE
/*******************************************************************************
 * Function Name : USB_HID_Send_Report.
 * Description   : Send HID Report Info to Host.
 * Input         : pHIDReport and reportSize.
 * Output        : None.
 * Return value  : None.
 *******************************************************************************/
void USB_HID_Send_Report(void *pHIDReport, size_t reportSize)
{
    //To Do
}
#endif

#ifdef USE_USB_OTG_FS
/**
 * @brief  This function handles OTG_FS_WKUP Handler.
 * @param  None
 * @retval None
 */
void OTG_FS_WKUP_irq(void)
{
    if(USB_OTG_dev.cfg.low_power)
    {
        *(uint32_t *)(0xE000ED10) &= 0xFFFFFFF9 ;
        SystemInit();
        USB_OTG_UngateClock(&USB_OTG_dev);
    }
    EXTI_ClearITPendingBit(EXTI_Line18);
}
#elif defined USE_USB_OTG_HS
/**
 * @brief  This function handles OTG_HS_WKUP Handler.
 * @param  None
 * @retval None
 */
void OTG_HS_WKUP_irq(void)
{
    if(USB_OTG_dev.cfg.low_power)
    {
        *(uint32_t *)(0xE000ED10) &= 0xFFFFFFF9 ;
        SystemInit();
        USB_OTG_UngateClock(&USB_OTG_dev);
    }
    EXTI_ClearITPendingBit(EXTI_Line20);
}
#endif

#ifdef USE_USB_OTG_FS
/**
 * @brief  This function handles OTG_FS Handler.
 * @param  None
 * @retval None
 */
void OTG_FS_irq(void)
{
    int mask = HAL_disable_irq();
    USBD_OTG_ISR_Handler(&USB_OTG_dev);
    HAL_enable_irq(mask);
}
#elif defined USE_USB_OTG_HS
/**
 * @brief  This function handles OTG_HS Handler.
 * @param  None
 * @retval None
 */
void OTG_HS_irq(void)
{
    int mask = HAL_disable_irq();
    USBD_OTG_ISR_Handler(&USB_OTG_dev);
    HAL_enable_irq(mask);
}
#endif

#ifdef USB_OTG_HS_DEDICATED_EP1_ENABLED
/**
 * @brief  This function handles OTG_HS_EP1_IN_IRQ Handler.
 * @param  None
 * @retval None
 */
void OTG_HS_EP1_IN_irq(void)
{
    int mask = HAL_disable_irq();
    USBD_OTG_EP1IN_ISR_Handler (&USB_OTG_dev);
    HAL_enable_irq(mask);
}

/**
 * @brief  This function handles OTG_HS_EP1_OUT_IRQ Handler.
 * @param  None
 * @retval None
 */
void OTG_HS_EP1_OUT_irq(void)
{
    int mask = HAL_disable_irq();
    USBD_OTG_EP1OUT_ISR_Handler (&USB_OTG_dev);
    HAL_enable_irq(mask);
}
#endif

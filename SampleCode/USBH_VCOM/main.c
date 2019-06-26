/**************************************************************************//**
 * @file     main.c
 * @version  V1.00
 * $Revision: 1 $
 * $Date: 03/03/19 4:02p $
 * @brief    Use USB Host core driver and CDC driver. This sample demonstrates how
 *           to connect a CDC class VCOM device.
 *
 * @note
 * Copyright (C) 2019 Nuvoton Technology Corp. All rights reserved.
*****************************************************************************/
#include <stdio.h>
#include <string.h>

#include "nuc980.h"
#include "sys.h"
#include "etimer.h"
#include "usbh_lib.h"
#include "usbh_cdc.h"


#define MAX_VCOM_PORT      8

char Line[64];             /* Console input buffer */

typedef struct
{
    CDC_DEV_T  *cdev;
    LINE_CODING_T  line_code;
    int    checked;
}  VCOM_PORT_T;

VCOM_PORT_T   vcom_dev[MAX_VCOM_PORT];

extern int kbhit(void);

volatile uint32_t  _timer_tick;

void ETMR0_IRQHandler(void)
{
    _timer_tick ++;
    // clear timer interrupt flag
    ETIMER_ClearIntFlag(0);
}

uint32_t  get_ticks(void)
{
    return _timer_tick;
}

void Start_ETIMER0(void)
{
    // Enable ETIMER0 engine clock
    outpw(REG_CLK_PCLKEN0, inpw(REG_CLK_PCLKEN0) | (1 << 8));

    // Set timer frequency to 100 HZ
    ETIMER_Open(0, ETIMER_PERIODIC_MODE, 100);

    // Enable timer interrupt
    ETIMER_EnableInt(0);
    sysInstallISR(IRQ_LEVEL_1, IRQ_TIMER0, (PVOID)ETMR0_IRQHandler);
    sysSetLocalInterrupt(ENABLE_IRQ);
    sysEnableInterrupt(IRQ_TIMER0);

    _timer_tick = 0;

    // Start Timer 0
    ETIMER_Start(0);
}

void delay_us(int usec)
{
    volatile int  loop = 300 * usec;
    while (loop > 0) loop--;
}

void  vcom_status_callback(CDC_DEV_T *cdev, uint8_t *rdata, int data_len)
{
    int  i, slot;

    slot = (int)cdev->client;

    printf("[VCOM%d STS] ", slot);
    for(i = 0; i < data_len; i++)
        printf("0x%02x ", rdata[i]);
    printf("\n");
}

void  vcom_rx_callback(CDC_DEV_T *cdev, uint8_t *rdata, int data_len)
{
    int   i, slot;

    slot = (int)cdev->client;
    //printf("[%d][RX %d] ", cdev->iface_cdc->if_num, data_len);
    printf("[RX][VCOM%d]: ", slot);
    for (i = 0; i < data_len; i++)
    {
        //printf("0x%02x ", rdata[i]);
        printf("%c", rdata[i]);
    }
    printf("\n");
}

void show_line_coding(LINE_CODING_T *lc)
{
    printf("[CDC device line coding]\n");
    printf("====================================\n");
    printf("Baud rate:  %d bps\n", lc->baud);
    printf("Parity:     ");
    switch (lc->parity)
    {
    case 0:
        printf("None\n");
        break;
    case 1:
        printf("Odd\n");
        break;
    case 2:
        printf("Even\n");
        break;
    case 3:
        printf("Mark\n");
        break;
    case 4:
        printf("Space\n");
        break;
    default:
        printf("Invalid!\n");
        break;
    }
    printf("Data Bits:  ");
    switch (lc->data_bits)
    {
    case 5 :
    case 6 :
    case 7 :
    case 8 :
    case 16:
        printf("%d\n", lc->data_bits);
        break;
    default:
        printf("Invalid!\n");
        break;
    }
    printf("Stop Bits:  %s\n\n", (lc->stop_bits == 0) ? "1" : ((lc->stop_bits == 1) ? "1.5" : "2"));
}

int  init_cdc_device(CDC_DEV_T *cdev, int slot)
{
    int     ret;
    LINE_CODING_T  *line_code;

    printf("\n\n===  VCOM%d  ===============================\n", slot);
    printf("  Init CDC device : 0x%x\n", (int)cdev);
    printf("  VID: 0x%x, PID: 0x%x, interface: %d\n\n", cdev->udev->descriptor.idVendor, cdev->udev->descriptor.idProduct, cdev->iface_cdc->if_num);

    line_code = &(vcom_dev[slot].line_code);

    ret = usbh_cdc_get_line_coding(cdev, line_code);
    if (ret < 0)
    {
        printf("Get Line Coding command failed: %d\n", ret);
    }
    else
        show_line_coding(line_code);

    line_code->baud = 115200;
    line_code->parity = 0;
    line_code->data_bits = 8;
    line_code->stop_bits = 0;

    ret = usbh_cdc_set_line_coding(cdev, line_code);
    if (ret < 0)
    {
        printf("Set Line Coding command failed: %d\n", ret);
    }

    ret = usbh_cdc_get_line_coding(cdev, line_code);
    if (ret < 0)
    {
        printf("Get Line Coding command failed: %d\n", ret);
    }
    else
    {
        printf("New line coding =>\n");
        show_line_coding(line_code);
    }

    usbh_cdc_set_control_line_state(cdev, 1, 1);

    printf("usbh_cdc_start_polling_status...\n");
    usbh_cdc_start_polling_status(cdev, vcom_status_callback);

    printf("usbh_cdc_start_to_receive_data...\n");
    usbh_cdc_start_to_receive_data(cdev, vcom_rx_callback);

    return 0;
}

void update_vcom_device()
{
    int    i, free_slot;
    CDC_DEV_T   *cdev;

    for (i = 0; i < MAX_VCOM_PORT; i++)
        vcom_dev[i].checked = 0;

    cdev = usbh_cdc_get_device_list();
    while (cdev != NULL)
    {
        free_slot = -1;
        for (i = MAX_VCOM_PORT-1; i >= 0; i--)
        {
            if (vcom_dev[i].cdev == NULL)
                free_slot = i;

            if ((vcom_dev[i].cdev == cdev) && (i == (int)cdev->client))
            {
                vcom_dev[i].checked = 1;
                break;
            }
        }

        printf("free_slot %d\n", free_slot);

        if (i < 0)      /* not found in VCOM device list, add it */
        {
            if (free_slot == -1)
            {
                printf("No free VCOM device slots!\n");
                goto next_cdev;
            }

            i = free_slot;
            vcom_dev[i].cdev = cdev;
            cdev->client = (void *)i;
            init_cdc_device(cdev, i);
            vcom_dev[i].checked = 1;
        }

next_cdev:
        cdev = cdev->next;
    }

    for (i = 0; i < MAX_VCOM_PORT; i++)
    {
        if ((vcom_dev[i].cdev != NULL) && (vcom_dev[i].checked == 0))
        {
            vcom_dev[i].cdev = NULL;
        }
    }
}

void UART_Init()
{
    /* enable UART0 clock */
    outpw(REG_CLK_PCLKEN0, inpw(REG_CLK_PCLKEN0) | 0x10000);

    /* GPF11, GPF12 */
    outpw(REG_SYS_GPF_MFPH, (inpw(REG_SYS_GPF_MFPH) & 0xfff00fff) | 0x11000);   // UART0 multi-function

    /* UART0 line configuration for (115200,n,8,1) */
    outpw(REG_UART0_LCR, inpw(REG_UART0_LCR) | 0x07);
    outpw(REG_UART0_BAUD, 0x30000066); /* 12MHz reference clock input, 115200 */
}


/*----------------------------------------------------------------------------
  MAIN function
 *----------------------------------------------------------------------------*/
int32_t main(void)
{
    CDC_DEV_T   *cdev;
    int         i, ret;
    char        message[64];

    sysDisableCache();
    sysFlushCache(I_D_CACHE);
    sysEnableCache(CACHE_WRITE_BACK);
    UART_Init();

    SYS_UnlockReg();
    outpw(REG_CLK_HCLKEN, inpw(REG_CLK_HCLKEN) | (1<<18));      /* enable USB Host clock   */
    outpw(REG_SYS_MISCFCR, (inpw(REG_SYS_MISCFCR) | (1<<11)));  /* set USRHDSEN as 1; USB host/device role selection decided by USBID (SYS_PWRON[16]) */
    outpw(REG_SYS_PWRON, (inpw(REG_SYS_PWRON) | (1<<16)));      /* set USB port 0 used for Host */

    // set PE.12  for USBH_PWREN
    outpw(REG_SYS_GPE_MFPH, (inpw(REG_SYS_GPE_MFPH) & ~0x000f0000) | 0x00010000);

    // set PE.10  for USB_OVC
    outpw(REG_SYS_GPE_MFPH, (inpw(REG_SYS_GPE_MFPH) & ~0x00000f00) | 0x00000100);

    printf("\n\n");
    printf("+--------------------------------------------+\n");
    printf("|                                            |\n");
    printf("|     USB Host VCOM sample program           |\n");
    printf("|                                            |\n");
    printf("+--------------------------------------------+\n");

    Start_ETIMER0();

    for (i = 0; i < MAX_VCOM_PORT; i++)
        vcom_dev[i].cdev = NULL;

    usbh_core_init();
    usbh_cdc_init();
    usbh_memory_used();

    while(1)
    {
        if (usbh_pooling_hubs())             /* USB Host port detect polling and management */
        {
            usbh_memory_used();              /* print out USB memory allocating information */

            if (usbh_cdc_get_device_list() == NULL)
            {
                /* There's no any VCOM device connected. */
                memset(vcom_dev, 0, sizeof(vcom_dev));
                continue;
            }

            update_vcom_device();
        }

        for (i = 0; i < MAX_VCOM_PORT; i++)
        {
            cdev = vcom_dev[i].cdev;
            if (cdev == NULL)
                continue;

            if (!cdev->rx_busy)
            {
                usbh_cdc_start_to_receive_data(cdev, vcom_rx_callback);
            }
        }

        /*
         *  Check user input and send data to CDC device immediately. For loopback test only.
         */
        if (!kbhit())
        {
            getchar();
            for (i = 0; i < MAX_VCOM_PORT; i++)
            {
                cdev = vcom_dev[i].cdev;
                if (cdev == NULL)
                    continue;

                memset(message, 0, sizeof(message));
                sprintf(message, "To VCOM%d (VID:0x%x, PID:0x%x, interface %d).\n",
                        i, cdev->udev->descriptor.idVendor, cdev->udev->descriptor.idProduct, cdev->iface_cdc->if_num);

                ret = usbh_cdc_send_data(cdev, (uint8_t *)message, 64);
                if (ret != 0)
                    printf("\n!! Send data failed, 0x%x!\n", ret);
            }

        }
    }
}


/*** (C) COPYRIGHT 2019 Nuvoton Technology Corp. ***/

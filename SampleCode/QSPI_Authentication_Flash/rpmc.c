/**************************************************************************//**
* @file     rpmc.c
* @brief    RPMC library
*
* SPDX-License-Identifier: Apache-2.0
* @copyright (C) 2018 Nuvoton Technology Corp. All rights reserved.
******************************************************************************/

#include <string.h>

#include "nuc980.h"
#include "sys.h"
#include "qspi.h"
#include "sha256.h"
#include "rpmc.h"

#define DEF_COUNTER_LEN      (4)
#define DEF_TAG_LEN          (12)
#define DEF_SIGNATURE_LEN    (32)
#define DEF_REG_READ_UID     (0x4B)
#define DEF_REG_READ_JEDEC   (0x9F)
#define DEF_RPMC_INSTRUCTION (0x9B)
#define DEF_RPMC_READ_STATUS (0x96)

enum
{
    CMDTYPE_WRITE_ROOTKEY,
    CMDTYPE_UPDATE_HMACKEY,
    CMDTYPE_INCREASE_COUNTER,
    CMDTYPE_REQUEST_COUNTER,
} RPMC_CMDTYPE;

/* Global usage for RPMC algorithm */
/* 32-bit counter data */
static uint8_t g_u8Counter[DEF_COUNTER_LEN];

/* Tag data usage for increase counter */
static uint8_t g_u8Tag[DEF_TAG_LEN];

/* Signature data usage for every instruction output */
static uint8_t g_u8Signature[DEF_SIGNATURE_LEN];

/* /CS: active */
#define RPMC_CS_LOW()    QSPI_SET_SS_LOW(QSPI_FLASH_PORT)

/* /CS: inactive */
#define RPMC_CS_HIGH()   QSPI_SET_SS_HIGH(QSPI_FLASH_PORT)

/**
  * @brief Read JEDECID
  * @param[in] Stored data buffer start address.
  * @return None
  */
void RPMC_ReadJEDECID(uint8_t *pu8Data)
{
    uint8_t u8IDCnt = 0;
    /* /CS: active */
    RPMC_CS_LOW();

    /* Send command: 0x9F, Read JEDEC READ-ID. */
    QSPI_WRITE_TX(QSPI_FLASH_PORT, DEF_REG_READ_JEDEC);

    /* Receive 24-bit */
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);

    /* Wait tx finish */
    while (QSPI_IS_BUSY(QSPI_FLASH_PORT));

    /* /CS: de-active */
    RPMC_CS_HIGH();

    /* Dummy read */
    QSPI_READ_RX(QSPI_FLASH_PORT);

    while (!QSPI_GET_RX_FIFO_EMPTY_FLAG(QSPI_FLASH_PORT))
        pu8Data[u8IDCnt++] = QSPI_READ_RX(QSPI_FLASH_PORT);
}

/**
  * @brief Read UID
  * @param[in] Stored data buffer start address.
  * @return None
  */
void RPMC_ReadUID(uint8_t *pu8Data)
{
    int32_t i;
    uint8_t u8IDCnt = 0;

    /* /CS: active */
    RPMC_CS_LOW();

    /* Send command: 0x4B, Read UID. */
    QSPI_WRITE_TX(QSPI_FLASH_PORT, DEF_REG_READ_UID);

    /* Send 32-bit dummy */
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);

    /* Wait tx finish */
    while (QSPI_IS_BUSY(QSPI_FLASH_PORT));

    QSPI_ClearRxFIFO(QSPI_FLASH_PORT);

    /* Receive 8-byte */
    for (i = 0; i < 8; i++)
    {
        while (QSPI_GET_TX_FIFO_FULL_FLAG(QSPI_FLASH_PORT));
        /* Receive */
        QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);
        if (!QSPI_GET_RX_FIFO_EMPTY_FLAG(QSPI_FLASH_PORT))
            pu8Data[u8IDCnt ++] = QSPI_READ_RX(QSPI_FLASH_PORT);
    }

    /* Wait tx finish */
    while (QSPI_IS_BUSY(QSPI_FLASH_PORT));

    /* /CS: de-active */
    RPMC_CS_HIGH();

    while (!QSPI_GET_RX_FIFO_EMPTY_FLAG(QSPI_FLASH_PORT))
        pu8Data[u8IDCnt ++] = QSPI_READ_RX(QSPI_FLASH_PORT);
}

/**
  * @brief SPI BYTE transfer
  * @param[in] Transferred byte.
  * @return Received byte.
  */
uint8_t RPMC_ByteTransfer(uint8_t DI)
{
    QSPI_WRITE_TX(QSPI_FLASH_PORT, DI);

    /* Wait tx finish */
    while (QSPI_IS_BUSY(QSPI_FLASH_PORT));

    return QSPI_READ_RX(QSPI_FLASH_PORT);
}

/**
  * @brief Read Monotonic Counter.
  * @param[in] None.
  * @return 32-bit monotonic counter value
  */
uint32_t RPMC_ReadCounter(void)
{
    return (((((g_u8Counter[0] * 0x100) + g_u8Counter[1]) * 0x100) + g_u8Counter[2]) * 0x100) + g_u8Counter[3];
}

/**
  * @brief Read status.
  * @param[in] Stored data buffer start address. 0: Read out RPMC status only, 1: Read out counter, tag, signature information.
  * @return RPMC status.
  */
uint32_t RPMC_ReadStatus(uint32_t u32ChkoutFlag)
{
    uint32_t u32RPMCStatus = 0;

    /* /CS: active */
    RPMC_CS_LOW();

    /* Read RPMC status command */
    RPMC_ByteTransfer(DEF_RPMC_READ_STATUS);
    RPMC_ByteTransfer(0x00);

    u32RPMCStatus = RPMC_ByteTransfer(0x00) & 0xFF;

    /* Checkout tag, counter and signature. */
    if (u32ChkoutFlag)
    {
        int i;

        /* Read tag */
        for (i = 0; i < sizeof(g_u8Tag); i++)
        {
            g_u8Tag[i] = RPMC_ByteTransfer(0x00);
        }

        /* Read counter */
        for (i = 0; i < sizeof(g_u8Counter); i++)
        {
            g_u8Counter[i] = RPMC_ByteTransfer(0x00);
        }

        /* Read signature */
        for (i = 0; i < sizeof(g_u8Signature); i++)
        {
            g_u8Signature[i] = RPMC_ByteTransfer(0x00);
        }
    }

    /* /CS: de-active */
    RPMC_CS_HIGH();

    return u32RPMCStatus;
}

/**
  * @brief Request counter.
  * @param[in] Selected counter address, from 1~4.
  * @param[in] 32-byte HMACKEY which is generated by RPMC_UpdateHMACKey().
  * @param[in] 12-byte input Tag data, which can be time stamp, serial number or random number.
  * @return RPMC status.
  * @note These return data would repeat after invoking RPMC_RequestCounter() operation.
  */
void RPMC_RequestCounter(uint32_t u32CntAddr, uint8_t *pu8HMACKey, uint8_t *pu8InTag)
{
    int i;
    uint8_t u8Message[16];
    u8Message[0] = DEF_RPMC_INSTRUCTION;
    u8Message[1] = CMDTYPE_REQUEST_COUNTER;
    u8Message[2] = u32CntAddr - 1;
    u8Message[3] = 0x00;
    memcpy((void *)&u8Message[4], (void *)pu8InTag, sizeof(u8Message) - 4);

    HMAC_SHA256(&pu8HMACKey[0], 32, &u8Message[0], sizeof(u8Message), &g_u8Signature[0]);

    /* /CS: active */
    RPMC_CS_LOW();

    /* Send instruction, CMD type, Counter address, Reserve and 96-bit Tag */
    for (i = 0; i < sizeof(u8Message); i++)
    {
        RPMC_ByteTransfer(u8Message[i]);
    }

    /* Send 256-bit Signature */
    for (i = 0; i < sizeof(g_u8Signature); i++)
    {
        RPMC_ByteTransfer(g_u8Signature[i]);
    }

    /* /CS: de-active */
    RPMC_CS_HIGH();
}

/**
  * @brief Write Rootkey.
  * @param[in] Selected counter address, from 1~4.
  * @param[in] 32-byte rootkey information.
  * @return RPMC status.
  */
uint32_t RPMC_WriteRootKey(uint32_t u32CntAddr, uint8_t *pu8RootKey)
{
    int i;
    uint8_t u8Message[4];
    uint32_t u32RPMCStatus;

    u8Message[0] = DEF_RPMC_INSTRUCTION;
    u8Message[1] = CMDTYPE_WRITE_ROOTKEY;
    u8Message[2] = u32CntAddr - 1;
    u8Message[3] = 0x00;

    HMAC_SHA256(&pu8RootKey[0], 32, &u8Message[0], sizeof(u8Message), &g_u8Signature[0]);

    /* /CS: active */
    RPMC_CS_LOW();

    /* Send instruction, CMD type, Counter address and Reserve. */
    for (i = 0; i < sizeof(u8Message); i++)
    {
        RPMC_ByteTransfer(u8Message[i]);
    }

    /* Send 256-bit Rootkey */
    for (i = 0; i < 32; i++)
    {
        RPMC_ByteTransfer(pu8RootKey[i]);
    }

    /* Send 224-bit Truncated Sign */
    for (i = 0; i < 28; i++)
    {
        RPMC_ByteTransfer(g_u8Signature[4 + i]);
    }

    /* /CS: de-active */
    RPMC_CS_HIGH();

    /* Wait until RPMC operation done */
    while ((u32RPMCStatus = RPMC_ReadStatus(0)) & 0x01);

    return u32RPMCStatus;
}

/**
  * @brief Update HMAC Key.
  * @param[in] Selected counter address, from 1~4.
  * @param[in] 32-byte rootkey information.
  * @param[in] 4-byte input hmac message data, which can be time stamp, serial number or random number.
  * @param[in] 32 byte HMACKEY, which would be use for increase/request counter after RPMC_UpdateHMACKey() operation success.
  * @return RPMC status.
  */
uint32_t RPMC_UpdateHMACKey(uint32_t u32CntAddr, uint8_t *pu8RootKey, uint8_t *pu8HMAC, uint8_t *pu8HMACKey)
{
    int i;
    uint8_t u8Message[8] = {0};
    uint32_t u32RPMCStatus;

    u8Message[0] = DEF_RPMC_INSTRUCTION;
    u8Message[1] = CMDTYPE_UPDATE_HMACKEY;
    u8Message[2] = u32CntAddr - 1;
    u8Message[3] = 0x00;
    memcpy((void *)&u8Message[4], (void *)&pu8HMAC[0], 4);

    /* Use rootkey generate HMAC key by SHA256 */
    HMAC_SHA256(&pu8RootKey[0], 32, &pu8HMAC[0], 4, &pu8HMACKey[0]);

    /* Calculate signature using SHA256 */
    HMAC_SHA256(&pu8HMACKey[0], 32, &u8Message[0], sizeof(u8Message), &g_u8Signature[0]);

    /* /CS: active */
    RPMC_CS_LOW();

    /* Send instruction, CMD type, Counter address, Reserve and 32-bit Keydata. */
    for (i = 0; i < sizeof(u8Message); i++)
    {
        RPMC_ByteTransfer(u8Message[i]);
    }

    /* Send 256-bit Truncated Sign */
    for (i = 0; i < 32; i++)
    {
        RPMC_ByteTransfer(g_u8Signature[i]);
    }

    /* /CS: de-active */
    RPMC_CS_HIGH();

    /* Wait until RPMC operation done */
    while ((u32RPMCStatus = RPMC_ReadStatus(0))& AF_REG_STATUS_BUSY);

    return u32RPMCStatus;
}

/**
  * @brief Increase counter.
  * @param[in] Selected counter address, from 1~4.
  * @param[in] 32-byte HMACKEY, which would be use for increase/request counter after RPMC_UpdateHMACKey()
  * @param[in] 12-byte input Tag data, which can be time stamp, serial number or random number.
  * @return RPMC status.
  * @note These returned data would repeat after invoking RPMC_RequestCounter() operation
  */
uint32_t RPMC_IncreaseCounter(uint32_t u32CntAddr, uint8_t *pu8HMACKey, uint8_t *pu8InTag)
{
    int i;
    uint8_t u8Message[8] = {0};
    uint32_t u32RPMCStatus;

    RPMC_RequestCounter(u32CntAddr, pu8HMACKey, pu8InTag);

    /* Wait until RPMC operation done */
    while ((u32RPMCStatus = RPMC_ReadStatus(0)) & 0x01);

    /* Checkout all. */
    RPMC_ReadStatus(1);

    u8Message[0] = DEF_RPMC_INSTRUCTION;
    u8Message[1] = CMDTYPE_INCREASE_COUNTER;
    u8Message[2] = u32CntAddr - 1;
    u8Message[3] = 0x00;
    memcpy((void *)&u8Message[4], &g_u8Counter[0], sizeof(g_u8Counter));

    /* Calculate signature by SHA256 */
    HMAC_SHA256(pu8HMACKey, 32, u8Message, sizeof(u8Message), g_u8Signature);

    /* /CS: active */
    RPMC_CS_LOW();

    /* Send instruction, CMD type, Counter address, Reserve and 32-bit Counter data. */
    for (i = 0; i < sizeof(u8Message); i++)
    {
        RPMC_ByteTransfer(u8Message[i]);
    }

    /* Send 256-bit Signature */
    for (i = 0; i < 32; i++)
    {
        RPMC_ByteTransfer(g_u8Signature[i]);
    }

    /* /CS: de-active */
    RPMC_CS_HIGH();

    /* Wait until RPMC operation done */
    while ((u32RPMCStatus = RPMC_ReadStatus(0))& AF_REG_STATUS_BUSY);

    return u32RPMCStatus;
}

/**
  * @brief Challenge signature.
  * @param[in] Selected counter address, from 1~4.
  * @param[in] 32-byte HMACKEY, which would be use for increase/request counter after RPMC_UpdateHMACKey()
  * @param[in] 12-byte input Tag data, which can be time stamp, serial number or random number.
  * @return Challenge result.
  * @retval 0 Signature match.
  * @note These returned data would repeat after invoking RPMC_RequestCounter() operation
  */
int32_t RPMC_Challenge(uint32_t u32CntAddr, uint8_t *pu8HMACKey, uint8_t *pu8InTag)
{
    uint8_t u8Message[16] = {0};
    uint8_t u8VerifySignature[32];

    RPMC_RequestCounter(u32CntAddr, pu8HMACKey, pu8InTag);

    /* Wait until RPMC operation done */
    while ((RPMC_ReadStatus(0)&AF_REG_STATUS_BUSY));

    /* Checkout all. */
    RPMC_ReadStatus(1);

    memcpy((void *)&u8Message[0],  g_u8Tag, sizeof(g_u8Tag));
    memcpy((void *)&u8Message[12], g_u8Counter, sizeof(g_u8Counter));

    /* Verification signature should as same as security output */
    HMAC_SHA256(pu8HMACKey, 32, u8Message, sizeof(u8Message), &u8VerifySignature[0]);

    /* Compare Verification signature (computed by controller) and internal signature (return from security Flash by request counter operation) */
    return memcmp(&u8VerifySignature[0], &g_u8Signature[0], sizeof(u8VerifySignature));
}

/**
  * @brief A create Rootkey reference.
  * @param[in] spi flash UID.
  * @param[in] UID length.
  * @param[out] 32-byte outputted Rootkey buffer.
  * @return None.
  */
void RPMC_CreateRootKey(uint8_t *pu8ID, uint32_t u32IDLen, uint8_t *pu8Rootkey)
{
    uint8_t u8RootKeyTag[32] = {0};

    /* Specify your tags */
    u8RootKeyTag[0] = 'N';
    u8RootKeyTag[1] = 'u';
    u8RootKeyTag[2] = 'v';
    u8RootKeyTag[3] = 'o';
    u8RootKeyTag[4] = 't';
    u8RootKeyTag[5] = 'o';
    u8RootKeyTag[6] = 'n';

    HMAC_SHA256(pu8ID, u32IDLen, &u8RootKeyTag[0], sizeof(u8RootKeyTag), pu8Rootkey);
}

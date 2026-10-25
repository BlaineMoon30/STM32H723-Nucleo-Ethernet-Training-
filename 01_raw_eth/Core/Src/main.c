/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "string.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* ---- 핸즈온 1 설정 ------------------------------------------------------------------------ */
#define RX_BUF_SIZE        1536U                    /* = CubeMX ETH 'Rx Buffers Length'          */
#define RX_POOL_CNT        (ETH_RX_DESC_CNT + 4U)   /* 링에 달린 4개 + 앱이 잡고 있는 여유 4개     */
#define RAW_ETHERTYPE      0x88B5U                  /* IEEE 802 로컬 실험용 EtherType             */
#define TX_PERIOD_MS       1000U
#define LINK_POLL_MS       500U
/* LAN8742A 레지스터 (IEEE 802.3 Clause 22 + 벤더) */
#define PHY_BCR            0U
#define PHY_BSR            1U
#define PHY_ID1            2U
#define PHY_ID2            3U
#define PHY_SMR            18U                      /* Special Modes: PHYAD[4:0], MODE[7:5]       */
#define PHY_SCSR           31U                      /* Special Control/Status: bit4:2 속도·듀플렉스 */
#define BSR_LINK           (1U << 2)
#define BSR_AN_DONE        (1U << 5)
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
#if defined ( __ICCARM__ ) /*!< IAR Compiler */
#pragma location=0x30000000
ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
#pragma location=0x30000200
ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

#elif defined ( __CC_ARM )  /* MDK ARM Compiler */

__attribute__((at(0x30000000))) ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
__attribute__((at(0x30000200))) ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

#elif defined ( __GNUC__ ) /* GNU Compiler */

ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT] __attribute__((section(".RxDescripSection"))); /* Ethernet Rx DMA Descriptors */
ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT] __attribute__((section(".TxDescripSection")));   /* Ethernet Tx DMA Descriptors */
#endif

ETH_TxPacketConfig TxConfig;

COM_InitTypeDef BspCOMInit;

ETH_HandleTypeDef heth;

PCD_HandleTypeDef hpcd_USB_OTG_HS;

/* USER CODE BEGIN PV */
/* 수신 버퍼 풀: D2 SRAM 0x3000 0400~ (링커 .RxArraySection, MPU Region1 = Normal non-cacheable) */
static uint8_t RxPool[RX_POOL_CNT][RX_BUF_SIZE] __attribute__((section(".RxArraySection"), aligned(32)));
static volatile uint8_t RxPoolUsed[RX_POOL_CNT];

/* 송신 프레임: AXI SRAM(D1, 캐시 가능) — 송신 직전 SCB_CleanDCache_by_Addr가 필요한 예 */
static uint8_t frame[64] __attribute__((aligned(32)));

static volatile uint8_t  rx_flag;       /* RxCpltCallback → 메인 루프 */
static volatile uint8_t  tx_busy;       /* Transmit_IT ~ TxFreeCallback */
static volatile uint32_t eth_err;       /* ErrorCallback이 세움 */
static uint32_t RxLastLen;
static uint32_t phy_addr = 0xFFU;       /* MDIO 스캔으로 찾은 주소 (보드 스트랩 = 0) */
static uint8_t  link_up;
static uint32_t hello_cnt;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_ETH_Init(void);
static void MX_USB_OTG_HS_PCD_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* ---------------------------------------------------------------------------------------------
 * Step 3 — MDIO로 PHY 찾기: 주소 0~31을 읽어 ID1이 0xFFFF/0x0000이 아닌 곳이 PHY
 * ------------------------------------------------------------------------------------------- */
static void phy_scan(void)
{
  uint32_t id1, id2, smr;
  for (uint32_t a = 0; a < 32U; a++)
  {
    if ((HAL_ETH_ReadPHYRegister(&heth, a, PHY_ID1, &id1) == HAL_OK) && (id1 != 0xFFFFU) && (id1 != 0x0000U))
    {
      HAL_ETH_ReadPHYRegister(&heth, a, PHY_ID2, &id2);
      HAL_ETH_ReadPHYRegister(&heth, a, PHY_SMR, &smr);
      printf("PHY @%lu  ID1=0x%04lX  ID2=0x%04lX  (Special Modes: MODE=%lu PHYAD=%lu)\r\n",
             a, id1, id2, (smr >> 5) & 7U, smr & 0x1FU);
      if (phy_addr == 0xFFU) { phy_addr = a; }
    }
  }
  if (phy_addr == 0xFFU)
  {
    printf("!! PHY not found (all 0xFFFF): check SB72(MDIO) / SB64(MDC) / PHY power(JP5=3V3)\r\n");
  }
}

/* BSR의 Link Status는 latch-low → 두 번 읽어야 현재 값 */
static uint32_t phy_read_bsr(void)
{
  uint32_t bsr = 0;
  HAL_ETH_ReadPHYRegister(&heth, phy_addr, PHY_BSR, &bsr);
  HAL_ETH_ReadPHYRegister(&heth, phy_addr, PHY_BSR, &bsr);
  return bsr;
}

/* 링크 업이면 PHYSCSR로 협상 결과를 읽어 MAC(ETH_MACCR FES/DM)에 반영하고 DMA 시작 */
static void link_update(void)
{
  if (phy_addr == 0xFFU) { return; }
  uint32_t bsr = phy_read_bsr();
  uint8_t up = ((bsr & BSR_LINK) != 0U) && ((bsr & BSR_AN_DONE) != 0U);

  if (up && !link_up)
  {
    uint32_t scsr = 0;
    ETH_MACConfigTypeDef mc;
    HAL_ETH_ReadPHYRegister(&heth, phy_addr, PHY_SCSR, &scsr);
    uint32_t sp = (scsr >> 2) & 7U;            /* 001 10H, 101 10F, 010 100H, 110 100F */
    HAL_ETH_GetMACConfig(&heth, &mc);
    mc.Speed      = (sp & 2U) ? ETH_SPEED_100M : ETH_SPEED_10M;
    mc.DuplexMode = (sp & 4U) ? ETH_FULLDUPLEX_MODE : ETH_HALFDUPLEX_MODE;
    HAL_ETH_SetMACConfig(&heth, &mc);
    HAL_ETH_Start_IT(&heth);                   /* RX 디스크립터 OWN=1, 인터럽트 허용, TE/RE=1 */
    link_up = 1;
    printf("Link UP   %s %s-duplex\r\n", (sp & 2U) ? "100M" : "10M", (sp & 4U) ? "Full" : "Half");
  }
  else if (!up && link_up)
  {
    HAL_ETH_Stop_IT(&heth);
    link_up = 0;
    printf("Link DOWN\r\n");
  }
}

/* ---------------------------------------------------------------------------------------------
 * Step 4 — Raw 프레임 송신: DST=브로드캐스트, SRC=보드 MAC, Type=0x88B5, 데이터 "HELLO n"
 * ------------------------------------------------------------------------------------------- */
static void send_hello(void)
{
  ETH_BufferTypeDef tb;
  if (!link_up || tx_busy) { return; }

  memset(frame, 0, sizeof(frame));
  memset(frame, 0xFF, 6);                               /* DST = FF:FF:FF:FF:FF:FF        */
  memcpy(frame + 6, heth.Init.MACAddr, 6);              /* SRC = 02:00:00:11:22:33        */
  frame[12] = (uint8_t)(RAW_ETHERTYPE >> 8);
  frame[13] = (uint8_t)(RAW_ETHERTYPE & 0xFFU);
  snprintf((char *)frame + 14, 46, "HELLO %lu", hello_cnt++);

  /* frame[]은 캐시 가능 메모리 → DMA가 읽기 전에 캐시 내용을 SRAM으로 (32 B 정렬·64 B) */
  SCB_CleanDCache_by_Addr((uint32_t *)frame, sizeof(frame));

  memset(&tb, 0, sizeof(tb));
  tb.buffer = frame;
  tb.len    = sizeof(frame);
  tb.next   = NULL;
  TxConfig.Length   = sizeof(frame);
  TxConfig.TxBuffer = &tb;
  TxConfig.pData    = frame;
  tx_busy = 1;
  if (HAL_ETH_Transmit_IT(&heth, &TxConfig) != HAL_OK)  /* TDES 작성 → OWN=1 → 테일 갱신 */
  {
    tx_busy = 0;
    printf("Transmit_IT failed (err=0x%lx)\r\n", HAL_ETH_GetError(&heth));
  }
}

static void hexdump(const uint8_t *p, uint32_t n)
{
  if (n > 48U) { n = 48U; }
  for (uint32_t i = 0; i < n; i++)
  {
    printf("%02X%s", p[i], ((i & 15U) == 15U) ? "\r\n" : " ");
  }
  printf("\r\n");
}

static void RxBuf_Free(void *p)
{
  for (uint32_t i = 0; i < RX_POOL_CNT; i++)
  {
    if (p == RxPool[i]) { RxPoolUsed[i] = 0; return; }
  }
}

/* ---- HAL ETH 콜백 (수신 버퍼 공급 · 조각 연결 · 완료 알림) -------------------------------- */
void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
  for (uint32_t i = 0; i < RX_POOL_CNT; i++)
  {
    if (!RxPoolUsed[i]) { RxPoolUsed[i] = 1; *buff = RxPool[i]; return; }
  }
  *buff = NULL;                               /* 풀 고갈 → 이 RDES는 비어 있게 됨(RBU 원인) */
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
  if (*pStart == NULL) { *pStart = buff; }    /* 1536 B 버퍼 하나 = 프레임 하나 (1518 B 이하) */
  *pEnd = buff;
  RxLastLen = Length;
}

void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *h)  { (void)h; rx_flag = 1; }
void HAL_ETH_TxCpltCallback(ETH_HandleTypeDef *h)  { HAL_ETH_ReleaseTxPacket(h); }   /* TDES 회수 */
void HAL_ETH_TxFreeCallback(uint32_t *buff)        { (void)buff; tx_busy = 0; }
void HAL_ETH_ErrorCallback(ETH_HandleTypeDef *h)   { eth_err = HAL_ETH_GetDMAError(h); }
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ETH_Init();
  MX_USB_OTG_HS_PCD_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Initialize leds */
  BSP_LED_Init(LED_GREEN);
  BSP_LED_Init(LED_YELLOW);
  BSP_LED_Init(LED_RED);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Initialize COM1 port (115200, 8 bits (7-bit data + 1 stop bit), no parity */
  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  printf("\r\n=== Hands-on 1: raw Ethernet (no lwIP) ===\r\n");
  printf("DMARxDscrTab @0x%08lX  DMATxDscrTab @0x%08lX  RxPool @0x%08lX\r\n",
         (uint32_t)DMARxDscrTab, (uint32_t)DMATxDscrTab, (uint32_t)RxPool);
  phy_scan();

  uint32_t t_tx = HAL_GetTick(), t_link = 0;
  while (1)
  {
    if ((HAL_GetTick() - t_link) >= LINK_POLL_MS) { t_link = HAL_GetTick(); link_update(); }

    if ((HAL_GetTick() - t_tx) >= TX_PERIOD_MS)
    {
      t_tx = HAL_GetTick();
      send_hello();
      BSP_LED_Toggle(LED_GREEN);                    /* LD1 점멸 = 펌웨어 동작 중 */
    }

    if (rx_flag)
    {
      void *pkt = NULL;
      rx_flag = 0;
      while (HAL_ETH_ReadData(&heth, &pkt) == HAL_OK)  /* OWN=0 RDES → 앱 버퍼, 빈 RDES 재충전 */
      {
        const uint8_t *b = (const uint8_t *)pkt;
        printf("RX %lu B  type=0x%02X%02X  from %02X:%02X:%02X:%02X:%02X:%02X\r\n", RxLastLen,
               b[12], b[13], b[6], b[7], b[8], b[9], b[10], b[11]);
        hexdump(b, RxLastLen);
        RxBuf_Free(pkt);
      }
    }

    if (eth_err)
    {
      printf("ETH DMA error: DMACSR snapshot=0x%08lX (RBU/FBE?)\r\n", eth_err);
      eth_err = 0;
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 275;
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_1;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ETH Initialization Function
  * @param None
  * @retval None
  */
static void MX_ETH_Init(void)
{

  /* USER CODE BEGIN ETH_Init 0 */

  /* USER CODE END ETH_Init 0 */

   static uint8_t MACAddr[6];

  /* USER CODE BEGIN ETH_Init 1 */

  /* USER CODE END ETH_Init 1 */
  heth.Instance = ETH;
  MACAddr[0] = 0x02;
  MACAddr[1] = 0x00;
  MACAddr[2] = 0x00;
  MACAddr[3] = 0x11;
  MACAddr[4] = 0x22;
  MACAddr[5] = 0x33;
  heth.Init.MACAddr = &MACAddr[0];
  heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
  heth.Init.TxDesc = DMATxDscrTab;
  heth.Init.RxDesc = DMARxDscrTab;
  heth.Init.RxBuffLen = 1536;

  /* USER CODE BEGIN MACADDRESS */

  /* USER CODE END MACADDRESS */

  if (HAL_ETH_Init(&heth) != HAL_OK)
  {
    Error_Handler();
  }

  memset(&TxConfig, 0 , sizeof(ETH_TxPacketConfig));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;
  /* USER CODE BEGIN ETH_Init 2 */

  /* USER CODE END ETH_Init 2 */

}

/**
  * @brief USB_OTG_HS Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_OTG_HS_PCD_Init(void)
{

  /* USER CODE BEGIN USB_OTG_HS_PCD_Init 0 */

  /* USER CODE END USB_OTG_HS_PCD_Init 0 */

  /* USER CODE BEGIN USB_OTG_HS_PCD_Init 1 */

  /* USER CODE END USB_OTG_HS_PCD_Init 1 */
  hpcd_USB_OTG_HS.Instance = USB_OTG_HS;
  hpcd_USB_OTG_HS.Init.dev_endpoints = 9;
  hpcd_USB_OTG_HS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_OTG_HS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.phy_itface = USB_OTG_EMBEDDED_PHY;
  hpcd_USB_OTG_HS.Init.Sof_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.lpm_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.vbus_sensing_enable = ENABLE;
  hpcd_USB_OTG_HS.Init.use_dedicated_ep1 = DISABLE;
  hpcd_USB_OTG_HS.Init.use_external_vbus = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_HS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_OTG_HS_PCD_Init 2 */

  /* USER CODE END USB_OTG_HS_PCD_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(USB_FS_PWR_EN_GPIO_Port, USB_FS_PWR_EN_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : USB_FS_PWR_EN_Pin */
  GPIO_InitStruct.Pin = USB_FS_PWR_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(USB_FS_PWR_EN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_FS_OVCR_Pin */
  GPIO_InitStruct.Pin = USB_FS_OVCR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USB_FS_OVCR_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x30000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_32KB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER2;
  MPU_InitStruct.Size = MPU_REGION_SIZE_1KB;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

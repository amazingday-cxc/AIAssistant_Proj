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
#include "cmsis_os.h"
#include "gpio.h"
#include "fsmc.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bsp_gt1151.h"
#include "bsp_nt35510.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* GT1151 is mounted in portrait coordinates (480 x 800), while the LCD demo
 * runs in landscape (800 x 480).  Change these values if the panel firmware
 * reports a different coordinate range. */
#define GT1151_RAW_WIDTH       480U
#define GT1151_RAW_HEIGHT      800U
#define GT1151_MARK_RADIUS       8U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile BSP_NT35510_Status lcd_init_status;
volatile bool lcd_test_ready;
volatile uint16_t lcd_test_id;
volatile GT1151_Status gt1151_init_status;
volatile GT1151_TouchData gt1151_touch_data;
volatile uint32_t gt1151_frame_count;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */
static void GT1151_DemoDrawHeader(const char *id);
static bool GT1151_MapToLandscape(const GT1151_Point *point,
                                  uint16_t *screen_x, uint16_t *screen_y);
static void GT1151_DemoDrawTouch(const GT1151_TouchData *touch);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static void GT1151_DemoDrawHeader(const char *id)
{
  uint16_t status_color;
  const char *status_text;

  if (gt1151_init_status == GT1151_OK)
  {
    status_color = BSP_NT35510_COLOR_GREEN;
    status_text = "GT1151 READY";
  }
  else if (gt1151_init_status == GT1151_ID_MISMATCH)
  {
    status_color = BSP_NT35510_COLOR_YELLOW;
    status_text = "GOODIX ID MISMATCH";
  }
  else
  {
    status_color = BSP_NT35510_COLOR_RED;
    status_text = "GT1151 I2C ERROR";
  }

  BSP_NT35510_FillRect(0, 0, 800, 64, BSP_NT35510_COLOR_DARK_BLUE);
  BSP_NT35510_DrawString(12, 8, 300, 24, status_text,
                        BSP_NT35510_FONT_24, status_color,
                        BSP_NT35510_COLOR_DARK_BLUE, false);
  BSP_NT35510_DrawString(330, 8, 60, 24, "ID:",
                        BSP_NT35510_FONT_24, BSP_NT35510_COLOR_WHITE,
                        BSP_NT35510_COLOR_DARK_BLUE, false);
  BSP_NT35510_DrawString(390, 8, 100, 24, id,
                        BSP_NT35510_FONT_24, BSP_NT35510_COLOR_WHITE,
                        BSP_NT35510_COLOR_DARK_BLUE, false);
  BSP_NT35510_DrawString(12, 36, 700, 16,
                        "Touch panel to draw; X/Y below are raw values",
                        BSP_NT35510_FONT_16, BSP_NT35510_COLOR_WHITE,
                        BSP_NT35510_COLOR_DARK_BLUE, false);
}

static bool GT1151_MapToLandscape(const GT1151_Point *point,
                                  uint16_t *screen_x, uint16_t *screen_y)
{
  if ((point == NULL) || (screen_x == NULL) || (screen_y == NULL) ||
      (point->x >= GT1151_RAW_WIDTH) ||
      (point->y >= GT1151_RAW_HEIGHT))
  {
    return false;
  }

  /* The touch panel reports portrait coordinates (480 x 800).  The NT35510
   * landscape setting uses MADCTL 0xA0 (MY + MV), therefore the matching
   * clockwise transformation is: Xlcd = 799 - Ytouch, Ylcd = Xtouch. */
  *screen_x = (uint16_t)((GT1151_RAW_HEIGHT - 1U) - point->y);
  *screen_y = point->x;
  return true;
}

static void GT1151_DemoDrawTouch(const GT1151_TouchData *touch)
{
  uint8_t i;

  /* Clear only the live-data row; the drawing area is intentionally retained
   * so a finger leaves a visible track across the panel. */
  BSP_NT35510_FillRect(0, 64, 800, 32, BSP_NT35510_COLOR_BLACK);
  BSP_NT35510_DrawString(8, 70, 88, 16, "POINTS:",
                        BSP_NT35510_FONT_16, BSP_NT35510_COLOR_WHITE,
                        BSP_NT35510_COLOR_BLACK, false);
  BSP_NT35510_DrawUInt(96, 70, touch->count, 1,
                      BSP_NT35510_FONT_16, BSP_NT35510_COLOR_CYAN,
                      BSP_NT35510_COLOR_BLACK, false);

  for (i = 0; i < touch->count; i++)
  {
    const GT1151_Point *point = &touch->points[i];
    uint16_t text_x = (uint16_t)(150U + (uint16_t)i * 125U);

    BSP_NT35510_DrawString(text_x, 70, 18, 16, "X",
                          BSP_NT35510_FONT_16, BSP_NT35510_COLOR_WHITE,
                          BSP_NT35510_COLOR_BLACK, false);
    BSP_NT35510_DrawUInt((uint16_t)(text_x + 16U), 70, point->x, 3,
                        BSP_NT35510_FONT_16, BSP_NT35510_COLOR_YELLOW,
                        BSP_NT35510_COLOR_BLACK, false);
    BSP_NT35510_DrawString((uint16_t)(text_x + 60U), 70, 18, 16, "Y",
                          BSP_NT35510_FONT_16, BSP_NT35510_COLOR_WHITE,
                          BSP_NT35510_COLOR_BLACK, false);
    BSP_NT35510_DrawUInt((uint16_t)(text_x + 76U), 70, point->y, 3,
                        BSP_NT35510_FONT_16, BSP_NT35510_COLOR_YELLOW,
                        BSP_NT35510_COLOR_BLACK, false);

    uint16_t screen_x;
    uint16_t screen_y;

    if (GT1151_MapToLandscape(point, &screen_x, &screen_y))
    {
      BSP_NT35510_DrawCircle(screen_x, screen_y, GT1151_MARK_RADIUS,
                            BSP_NT35510_COLOR_RED);
      BSP_NT35510_DrawCircle(screen_x, screen_y,
                            (uint16_t)(GT1151_MARK_RADIUS - 3U),
                            BSP_NT35510_COLOR_YELLOW);
    }
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  // HAL_DMA_Start(&hdma_memtomem_dma2_stream0, )

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_FSMC_Init();
  /* USER CODE BEGIN 2 */
  lcd_init_status = BSP_NT35510_Init();
  lcd_test_id = BSP_NT35510_GetDeviceId();
  lcd_test_ready = BSP_NT35510_IsReady();
  (void)BSP_NT35510_SetOrientation(BSP_NT35510_ORIENTATION_LANDSCAPE);
  BSP_NT35510_Clear(BSP_NT35510_COLOR_BLACK);

  gt1151_init_status = BSP_GT1151_Init();
  char gt1151_id[5] = "----";
  if (gt1151_init_status != GT1151_ERROR)
  {
    (void)BSP_GT1151_ReadID(gt1151_id);
  }
  GT1151_DemoDrawHeader(gt1151_id);
  /* USER CODE END 2 */

  // /* Init scheduler */
  // osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
  // MX_FREERTOS_Init();
  //
  // /* Start scheduler */
  // osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (gt1151_init_status != GT1151_ERROR)
    {
      GT1151_TouchData touch;

      if (BSP_GT1151_Scan(&touch) > 0U)
      {
        gt1151_touch_data = touch;
        gt1151_frame_count++;
        GT1151_DemoDrawTouch(&touch);
      }
    }

    /* No RTOS is required: HAL_Delay uses the HAL time base (TIM6 here). */
    HAL_Delay(5);
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
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

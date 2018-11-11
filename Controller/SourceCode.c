#include "stm32f0xx.h"
#include "stm32f0xx_rcc.h"
#include "stm32f0xx_gpio.h"
#include "stm32f0xx_usart.h"
#include "stm32f0xx_misc.h"
#include "stm32f0xx_dma.h"
#include "stm32f0xx_adc.h"
#include "stm32f0xx_tim.h"
#include "stm32f0xx_dac.h"

#include <stdio.h>
#include <string.h>

#define BUTTON_LED_PIN_MASK (GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2)
#define BUTTON_PIN_MASK (GPIO_Pin_All ^ (BUTTON_LED_PIN_MASK | GPIO_Pin_3))

//10ms ～　1us
#define BUTTON_CHECK_INTERVAL 1000

// 4096 / (3.3V * 1000) = 1.2412 (1mVのサンプリング値)
// 1.2412 * 100 = 124.12 (バイアスは200mV)
#define VOLTAGE_BIAS 248

#define USART_BAUDRATE 1500000

#define USART_RECIEVE_SIZE 1
#define USART_TRANSFER_SIZE 16

#define ADC_CHANNEL_NUM 2

__IO unsigned char USART_ReceivedData[USART_RECIEVE_SIZE];
__IO unsigned char USART_TransferData[USART_TRANSFER_SIZE];

__IO unsigned short ADC_SampledData[ADC_CHANNEL_NUM];

uint16_t max_volume_vol[ADC_CHANNEL_NUM];
uint16_t min_volume_vol[ADC_CHANNEL_NUM];

uint16_t average_volume_vol[ADC_CHANNEL_NUM];
uint16_t diff_volume_vol[ADC_CHANNEL_NUM];

uint16_t oldButtonPinStat;
uint16_t oldVolumePinStat[ADC_CHANNEL_NUM];

unsigned char dma_wait_flag;

unsigned char ready = 0;
unsigned char indicator_on = 0;

DMA_InitTypeDef DMA_SendDataInitStructure;

int main(void)
{
	char buff[32];
	GPIO_InitTypeDef InitGpio;
	static unsigned int counter = 0;
	int endf = 0;
	int i;

	for(i = 0; i < ADC_CHANNEL_NUM; i++){
		max_volume_vol[i] = 0;
		min_volume_vol[i] = 0xFFFF;

		average_volume_vol[i] = 0;
	}

	dma_wait_flag = 0;

	SystemInit();

	GPIOInit();
	 USARTInit();
	 ADCInit();
	 DACInit();
	 DMAInit();

	 TIMInit();
	 StartPeriph();

	 for(i = 0; i < 100; i++);
	 SetLED(0);


	  while(1)
	  {
	  }

}

void SetLED(uint8_t light_mask)
{
	int i;

	 for(i = 8 - 1; i >= 0; i--){
		 GPIO_SetBits(GPIOB, GPIO_Pin_0);

		 if((light_mask >> i) & 1){
			 GPIO_ResetBits(GPIOB, GPIO_Pin_1);
		 }else{
			 GPIO_SetBits(GPIOB, GPIO_Pin_1);
		 }

		 GPIO_ResetBits(GPIOB, GPIO_Pin_0);
	 }
	 GPIO_SetBits(GPIOB, GPIO_Pin_2);
	 GPIO_ResetBits(GPIOB, GPIO_Pin_2);
}

void StartPeriph(void)
{
	USART_Cmd(USART1, ENABLE);

	ADC_Cmd(ADC1, ENABLE);
	while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_ADEN));
	ADC_StartOfConversion(ADC1);

	DAC_SetChannel1Data(DAC_Align_12b_R, 2048);
	DAC_SoftwareTriggerCmd(DAC_Channel_1, ENABLE);

	TIM_Cmd(TIM1, ENABLE);
}

void TIMInit(void)
{
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

	//Indicator LED
	TIM_TimeBaseStructure.TIM_Prescaler = 99;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseStructure.TIM_Period = (SystemCoreClock / 100 / 10) / 4;

	TIM_TimeBaseStructure.TIM_ClockDivision = 0;
	TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure);

	NVIC_InitStructure.NVIC_IRQChannel = TIM1_BRK_UP_TRG_COM_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
	TIM_ITConfig(TIM1, TIM_IT_Update, ENABLE);


	//Send Check Timer
	TIM_TimeBaseStructure.TIM_Prescaler = BUTTON_CHECK_INTERVAL - 1;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseStructure.TIM_Period = SystemCoreClock / 1000 / 1000;

	TIM_TimeBaseStructure.TIM_ClockDivision = 0;
	TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

	NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
}

void DMAInit(void)
{
	DMA_InitTypeDef DMA_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	  /* DMA1 clock enable */
	  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1 , ENABLE);

	  //USART TX
	  DMA_SendDataInitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART1->TDR;
	  DMA_SendDataInitStructure.DMA_MemoryBaseAddr = (uint32_t)USART_TransferData;
	  DMA_SendDataInitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
	  DMA_SendDataInitStructure.DMA_BufferSize = USART_TRANSFER_SIZE;
	  DMA_SendDataInitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	  DMA_SendDataInitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	  DMA_SendDataInitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	  DMA_SendDataInitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	  DMA_SendDataInitStructure.DMA_Mode = DMA_Mode_Normal;
	  DMA_SendDataInitStructure.DMA_Priority = DMA_Priority_High;
	  DMA_SendDataInitStructure.DMA_M2M = DMA_M2M_Disable;



	  //ADC
	  DMA_DeInit(DMA1_Channel1);
	  DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;
	  DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)ADC_SampledData;
	  DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
	  DMA_InitStructure.DMA_BufferSize = ADC_CHANNEL_NUM;
	  DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	  DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	  DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
	  DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
	  DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
	  DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;
	  DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
	  DMA_Init(DMA1_Channel1, &DMA_InitStructure);

	  DMA_Cmd(DMA1_Channel1, ENABLE);

	  NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel1_IRQn;
	  NVIC_InitStructure.NVIC_IRQChannelPriority = 0;
	  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	  NVIC_Init(&NVIC_InitStructure);

	  DMA_ClearITPendingBit(DMA1_IT_TC1);
	  DMA_ITConfig(DMA1_Channel1, DMA_IT_TC, ENABLE);
}

void GPIOInit(void)
{

    GPIO_InitTypeDef GPIO_InitStructure;

    /* Enable GPIO clock */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOC, ENABLE);

    //LED
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure);


    /* Configure USART Tx, Rx as alternate function push-pull */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* Connect PXx to USARTx_Tx */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_1);

    //Volumes
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    //Volume DAC
	GPIO_InitStructure.GPIO_Pin =  GPIO_Pin_4;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOA, &GPIO_InitStructure);


	//Buttons LED Control
    GPIO_InitStructure.GPIO_Pin = BUTTON_LED_PIN_MASK;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    //Init
	GPIO_SetBits(GPIOB, GPIO_Pin_0);
	GPIO_SetBits(GPIOB, GPIO_Pin_1);
	GPIO_ResetBits(GPIOB, GPIO_Pin_2);


    //Buttons without Pin3
    GPIO_InitStructure.GPIO_Pin = BUTTON_PIN_MASK;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
}

void DACInit(void)
{
	DAC_InitTypeDef DAC_InitStructure;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);

	DAC_DeInit();
    DAC_InitStructure.DAC_Trigger = DAC_Trigger_Software;
    DAC_InitStructure.DAC_OutputBuffer = DAC_OutputBuffer_Enable;
    DAC_Init(DAC_Channel_1, &DAC_InitStructure);

    DAC_Cmd(DAC_Channel_1, ENABLE);
}

void USARTInit(void)
{
    USART_InitTypeDef USART_InitStructure;
    USART_ClockInitTypeDef USART_ClockInitStruct;
    NVIC_InitTypeDef NVIC_InitStructure;

    USART_ClockStructInit(&USART_ClockInitStruct);
    USART_ClockInitStruct.USART_Clock = USART_Clock_Enable;
    USART_ClockInit(USART1, &USART_ClockInitStruct);


    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

    USART_InitStructure.USART_BaudRate = USART_BAUDRATE;
    USART_InitStructure.USART_WordLength = USART_WordLength_9b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_Odd;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx;

    /* USART configuration */
    USART_Init(USART1, &USART_InitStructure);
}

void ADCInit(void)
{
  ADC_InitTypeDef     ADC_InitStructure;
  DMA_InitTypeDef     DMA_InitStructure;

  /* ADC1 DeInit */
  ADC_DeInit(ADC1);

  /* ADC1 Periph clock enable */
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

  ADC_DeInit(ADC1);

  /* Initialize ADC structure */

  ADC_StructInit(&ADC_InitStructure);

  /* Configure the ADC1 in continous mode withe a resolutuion equal to 12 bits  */
  ADC_InitStructure.ADC_Resolution = ADC_Resolution_12b;
  ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;
  ADC_InitStructure.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
  ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
  ADC_InitStructure.ADC_ScanDirection = ADC_ScanDirection_Upward;
  ADC_Init(ADC1, &ADC_InitStructure);

  /* Convert the ADC1 temperature sensor  with 55.5 Cycles as sampling time */
  ADC_ChannelConfig(ADC1, ADC_Channel_1 , ADC_SampleTime_71_5Cycles);
  ADC_ChannelConfig(ADC1, ADC_Channel_2 , ADC_SampleTime_71_5Cycles);

  ADC_GetCalibrationFactor(ADC1);

  /* ADC DMA request in circular mode */
  ADC_DMARequestModeConfig(ADC1, ADC_DMAMode_Circular);

  /* Enable ADC_DMA */
  ADC_DMACmd(ADC1, ENABLE);
}

void USART_DMASend(int length)
{
	if(dma_wait_flag == 0){
		dma_wait_flag = 1;

		while(DMA_GetCurrDataCounter(DMA1_Channel2) > 0);

		DMA_SendDataInitStructure.DMA_BufferSize = length;

		DMA_DeInit(DMA1_Channel2);
		DMA_Init(DMA1_Channel2, &DMA_SendDataInitStructure);

		USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE);
		DMA_Cmd(DMA1_Channel2, ENABLE);

		dma_wait_flag = 0;

		indicator_on = 1;
	}
}

void TIM1_BRK_UP_TRG_COM_IRQHandler(void)
{
	static unsigned char toggle_bit = 0;
	static int counter = 0;
	int i = 0;
	uint16_t Button_Stat;

	static unsigned int button_led_start_demo = 0;

	if(TIM_GetITStatus(TIM1, TIM_IT_Update) == SET){
		TIM_ClearITPendingBit(TIM1, TIM_IT_Update);

		if(ready == 0 || ready == 1){
			switch(button_led_start_demo){
			case 5: SetLED(0b01100000); break;
			case 11: SetLED(0b00010010); break;
			case 17: SetLED(0b00001100); break;
			case 23: SetLED(0b00000001); break;
			case 29: SetLED(0xFF); break;
			}

			++button_led_start_demo;
		}

		if(ready == 0){
			if(++counter == 20){
				counter = 0;

				ready = 1;
			}else{
				if(toggle_bit == 0){
					GPIO_SetBits(GPIOC, GPIO_Pin_9);
				}else{
					GPIO_ResetBits(GPIOC, GPIO_Pin_9);
				}

				toggle_bit = !toggle_bit;
			}
		}else if(ready == 1){
			if(++counter == 20){
				counter = 0;
				ready = 2;

				DMA_ITConfig(DMA1_Channel1, DMA_IT_TC, DISABLE);
				GPIO_SetBits(GPIOC, GPIO_Pin_9);

				for(i = 0; i < ADC_CHANNEL_NUM; i++){
					diff_volume_vol[i]
					                = max_volume_vol[i] - min_volume_vol[i];
				}

				TIM_Cmd(TIM2, ENABLE);
			}else{
				if(toggle_bit == 0){
					GPIO_SetBits(GPIOC, GPIO_Pin_9);
				}else{
					GPIO_ResetBits(GPIOC, GPIO_Pin_9);
				}

				toggle_bit = !toggle_bit;
			}
		}else{
			if(indicator_on == 1){
				if(toggle_bit == 0){
					GPIO_SetBits(GPIOC, GPIO_Pin_8);
				}else{
					GPIO_ResetBits(GPIOC, GPIO_Pin_8);
				}

				toggle_bit = !toggle_bit;

				if(++counter == 5){
					counter = 0;

					GPIO_ResetBits(GPIOC, GPIO_Pin_8);
					indicator_on = 0;
				}
			}
		}
	}
}

#define TXD_IDENTIFER_SIZE 4
#define TXD_HEADER_SIZE (TXD_IDENTIFER_SIZE + sizeof(uint16_t))

void TIM2_IRQHandler(void)
{
	int i;
	unsigned char exec;
	uint16_t ButtonPinStat;
	uint16_t VolumePinStat[ADC_CHANNEL_NUM];
	uint16_t blockSize;
	uint16_t txd_ptr;
	static int check_counter = 0;

	if(TIM_GetITStatus(TIM2, TIM_IT_Update) == SET){
		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

		ButtonPinStat = GPIOB->IDR & BUTTON_PIN_MASK;
		for(i = 0; i < ADC_CHANNEL_NUM; i++){
		  if(ADC_SampledData[i] >= average_volume_vol[i] + diff_volume_vol[i] + VOLTAGE_BIAS){
			  VolumePinStat[i] = 1;
		  }else if(ADC_SampledData[i] <= average_volume_vol[i] - diff_volume_vol[i] - VOLTAGE_BIAS){
			  VolumePinStat[i] = 2;
		  }else{
			  VolumePinStat[i] = 0;
		  }
		}

		exec = 0;

		if(ButtonPinStat != oldButtonPinStat){
			oldButtonPinStat = ButtonPinStat;
			exec = 1;
		}
		for(i = 0; i < ADC_CHANNEL_NUM; i++){
		  if(VolumePinStat[i] != oldVolumePinStat[i]){
			  oldVolumePinStat[i] = VolumePinStat[i];
			  exec = 1;
		  }
		}

		++check_counter;
		if(exec == 1 || check_counter == 100){
			check_counter = 0;

			blockSize = 0;

			memcpy(&USART_TransferData[0], "BVST", TXD_IDENTIFER_SIZE);
			// *((uint16_t *)&USART_TransferData[4]) = ブロックサイズ;

			*((uint16_t *)&USART_TransferData[TXD_HEADER_SIZE + blockSize])
					= ButtonPinStat >> 4;
			blockSize += 2;

			for(i = 0; i < ADC_CHANNEL_NUM; i++){
				USART_TransferData[TXD_HEADER_SIZE + blockSize]
				                   = VolumePinStat[i];
				blockSize += 1;
			}

			*((uint16_t *)&USART_TransferData[TXD_IDENTIFER_SIZE]) = blockSize;

			USART_DMASend(TXD_HEADER_SIZE + blockSize);

			SetLED(~((ButtonPinStat >> 4) & 0xFF));
		}
	}
}

void DMA1_Channel1_IRQHandler(void)
{
	int i;
	uint16_t dac_value;
	uint16_t channels_average;

	if(DMA_GetITStatus(DMA1_IT_TC1) == SET)
	{
		DMA_ClearITPendingBit(DMA1_IT_TC1);

		if(ready == 0){
			channels_average = 0;

			for(i = 0; i < ADC_CHANNEL_NUM; i++){
				channels_average += ADC_SampledData[i];
				if(i > 0){
					channels_average >>= 1;
				}
			}

			//ADCの入力からDACの出力電圧を変動させる
			dac_value = DAC_GetDataOutputValue(DAC_Channel_1);

			if(channels_average >= 2048){
				dac_value -= channels_average - 2048;
			}else{
				dac_value += 2048 - channels_average;
			}

			DAC_SetChannel1Data(DAC_Align_12b_R, dac_value);
			DAC_SoftwareTriggerCmd(DAC_Channel_1, ENABLE);
		}else if(ready = 1){
			for(i = 0; i < ADC_CHANNEL_NUM; i++){
				if(ADC_SampledData[i] > max_volume_vol[i]){
					max_volume_vol[i] = ADC_SampledData[i];
				}
				if(ADC_SampledData[i] < min_volume_vol[i]){
					min_volume_vol[i] = ADC_SampledData[i];
				}

				if(average_volume_vol[i] > 0){
					average_volume_vol[i] += ADC_SampledData[i];
					average_volume_vol[i] >>= 1;
				}else{
					average_volume_vol[i] = ADC_SampledData[i];
				}
			}
		}
	}
}


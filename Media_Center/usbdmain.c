#include "LPC17xx.h"                        /* LPC17xx definitions */
#include "type.h"

#include "usb.h"
#include "usbcfg.h"
#include "usbhw.h"
#include "usbcore.h"
#include "usbaudio.h"

#include <stdio.h>
#include <string.h>
#include "LPC17xx.H"                       
#include "GLCD.h"
#include "LED.h"
#include "ADC.h"
#include "KBD.h"

#define __FI        1                      /* Font index 16x24               */
#define __USE_LCD   0										/* Uncomment to use the LCD */

//ITM Stimulus Port definitions for printf //////////////////
#define ITM_Port8(n)    (*((volatile unsigned char *)(0xE0000000+4*n)))
#define ITM_Port16(n)   (*((volatile unsigned short*)(0xE0000000+4*n)))
#define ITM_Port32(n)   (*((volatile unsigned long *)(0xE0000000+4*n)))

#define DEMCR           (*((volatile unsigned long *)(0xE000EDFC)))
#define TRCENA          0x01000000

struct __FILE { int handle;  };
FILE __stdout;
FILE __stdin;

int fputc(int ch, FILE *f) {
  if (DEMCR & TRCENA) {
    while (ITM_Port32(0) == 0);
    ITM_Port8(0) = ch;
  }
  return(ch);
}
/////////////////////////////////////////////////////////

char keycode[10];
char direction[10];
int pointerLocation = 0;
int gameCharX = 120;
int gameCharY = 120;
int pipeX = 240;
int pipeY = 0;
int pipeX2 = 80;
int pipeY2 = 0;
int menuSelected = 1;
int photoGallerySelected = 0;
int mp3PlayerSelected = 0;
int gameSelected = 0;
int justOpened = 0;
int currentPic = 0;
int lastPic = -1;
int moveUp = 0;
int counter = 0;
int buttonPressed = 0;
int dead = 0;
int play = 0;
int activePipe = 1;
int activePipe2 = 3;
int gameover = 0;
int score = 0;
char scoreText[10];

//////Use to trace the pot values in Debug
//////uint16_t ADC_Dbg;

/* Import external variables from IRQ.c file                                  */
extern uint8_t  clock_ms;
extern unsigned char logo[];
extern unsigned char bg[];
extern unsigned char foreground[];
extern unsigned char bird_mid[];
extern unsigned char bird_up[];
extern unsigned char bird_down[];
extern unsigned char bird_dead[];
extern unsigned char pipe1[];
extern unsigned char pipe2[];
extern unsigned char pipe3[];

extern  void SystemClockUpdate(void);
extern uint32_t SystemFrequency;  
uint8_t  Mute;                                 /* Mute State */
uint32_t Volume;                               /* Volume Level */

#if USB_DMA
uint32_t *InfoBuf = (uint32_t *)(DMA_BUF_ADR);
short *DataBuf = (short *)(DMA_BUF_ADR + 4*P_C);
#else
uint32_t InfoBuf[P_C];
short DataBuf[B_S];                         /* Data Buffer */
#endif

uint16_t  DataOut;                              /* Data Out Index */
uint16_t  DataIn;                               /* Data In Index */

uint8_t   DataRun;                              /* Data Stream Run State */
uint16_t  PotVal;                               /* Potenciometer Value */
uint32_t  VUM;                                  /* VU Meter */
uint32_t  Tick;                                 /* Time Tick */


/*
 * Get Potenciometer Value
 */

void get_potval (void) {
  uint32_t val;

  LPC_ADC->CR |= 0x01000000;              /* Start A/D Conversion */
  do {
    val = LPC_ADC->GDR;                   /* Read A/D Data Register */
  } while ((val & 0x80000000) == 0);      /* Wait for end of A/D Conversion */
  LPC_ADC->CR &= ~0x01000000;             /* Stop A/D Conversion */
  PotVal = ((val >> 8) & 0xF8) +          /* Extract Potenciometer Value */
           ((val >> 7) & 0x08);
}


/*
 * Timer Counter 0 Interrupt Service Routine
 *   executed each 31.25us (32kHz frequency)
 */

void TIMER0_IRQHandler(void) 
{
  long  val;
  uint32_t cnt;

  if (DataRun) {                            /* Data Stream is running */
    val = DataBuf[DataOut];                 /* Get Audio Sample */
    cnt = (DataIn - DataOut) & (B_S - 1);   /* Buffer Data Count */
    if (cnt == (B_S - P_C*P_S)) {           /* Too much Data in Buffer */
      DataOut++;                            /* Skip one Sample */
    }
    if (cnt > (P_C*P_S)) {                  /* Still enough Data in Buffer */
      DataOut++;                            /* Update Data Out Index */
    }
    DataOut &= B_S - 1;                     /* Adjust Buffer Out Index */
    if (val < 0) VUM -= val;                /* Accumulate Neg Value */
    else         VUM += val;                /* Accumulate Pos Value */
    val  *= Volume;                         /* Apply Volume Level */
    val >>= 16;                             /* Adjust Value */
    val  += 0x8000;                         /* Add Bias */
    val  &= 0xFFFF;                         /* Mask Value */
  } else {
    val = 0x8000;                           /* DAC Middle Point */
  }

  if (Mute) {
    val = 0x8000;                           /* DAC Middle Point */
  }

  LPC_DAC->CR = val & 0xFFC0;             /* Set Speaker Output */

  if ((Tick++ & 0x03FF) == 0) {             /* On every 1024th Tick */
    get_potval();                           /* Get Potenciometer Value */
    if (VolCur == 0x8000) {                 /* Check for Minimum Level */
      Volume = 0;                           /* No Sound */
    } else {
      Volume = VolCur * PotVal;             /* Chained Volume Level */
    }
    val = VUM >> 20;                        /* Scale Accumulated Value */
    VUM = 0;                                /* Clear VUM */
    if (val > 7) val = 7;                   /* Limit Value */
  }

  LPC_TIM0->IR = 1;   	/* Clear Interrupt Flag */
	
	if(get_button() == KBD_LEFT){
		NVIC_DisableIRQ(TIMER0_IRQn);
		NVIC_DisableIRQ(USB_IRQn);
		USB_Connect(FALSE);		/* USB Disconnect */	
		mp3PlayerSelected = 0;
		menuSelected = 1;
		justOpened = 1;
		USB_Reset();
		USB_Connect(TRUE);		/* USB Connect */			
	}
}



/*****************************************************************************
**   Main Function  main()
******************************************************************************/
int main (void)
{
  volatile uint32_t pclkdiv, pclk;
	uint32_t joystickCode = 0;
	uint32_t lastCode = 1;
	
  LED_Init();                                /* LED Initialization            */
  ADC_Init();                                /* ADC Initialization            */
  KBD_Init();                                /* KBD Initialization            */
	GLCD_Init();                               /* Initialize graphical LCD */
	
	GLCD_Clear(White);                         /* Clear graphical LCD display   */
  GLCD_SetBackColor(Black);
  GLCD_SetTextColor(Yellow);
  GLCD_DisplayString(0, 0, __FI, "     Media Center     ");
	GLCD_DisplayString(1, 0, __FI, "      Select One      ");
	GLCD_DisplayString(2, 0, __FI, "                      ");
	GLCD_DisplayString(3, 0, __FI, "                      ");
	GLCD_DisplayString(4, 0, __FI, "                      ");
	GLCD_DisplayString(5, 0, __FI, "                      ");
	GLCD_DisplayString(6, 0, __FI, "                      ");
	GLCD_DisplayString(7, 0, __FI, "                      ");
	GLCD_DisplayString(8, 0, __FI, "                      ");
	GLCD_DisplayString(9, 0, __FI, "                      ");
	GLCD_DisplayString(10, 0, __FI, "                      ");
	GLCD_DisplayString(4, 5, __FI, "Photo Gallery   ");
	GLCD_DisplayString(6, 5, __FI, "MP3 Player      ");
	GLCD_DisplayString(8, 5, __FI, "Game            ");

	
  /********* The main function is an endless loop ***********/ 
  while (1) {                
			joystickCode = get_button();
		
			if(lastCode != joystickCode || gameSelected)
			{
				lastCode = joystickCode;				
				switch(joystickCode){
					case 0x00:
						if(menuSelected == 1)
						{
							sprintf(direction, "N"); 
							if(pointerLocation != 85 && pointerLocation != 135 && pointerLocation != 185) pointerLocation = 85;
						}
						break;					
					case KBD_LEFT:
						sprintf(direction, "L");	
						if(gameSelected && !justOpened){
							gameSelected = 0;
							menuSelected = 1;
							justOpened = 1;
						}
						if(photoGallerySelected && !justOpened){
							photoGallerySelected = 0;
							menuSelected = 1;
							justOpened = 1;
						}
						if(mp3PlayerSelected && !justOpened){
							mp3PlayerSelected = 0;
						
							menuSelected = 1;
							justOpened = 1;
						}
						break;
					case KBD_UP:
						sprintf(direction, "U"); 
						if(menuSelected == 1)
						{				
							if(pointerLocation == 135) pointerLocation = 85;
							else if(pointerLocation == 185) pointerLocation = 135;
						}
						break;
					case KBD_DOWN:
						sprintf(direction, "D"); 
						if(menuSelected == 1)
						{
							if(pointerLocation == 85) pointerLocation = 135;
							else if(pointerLocation == 135) pointerLocation = 185;
						}
						break;
					case 0x01:
						if(menuSelected == 1)
						{
							sprintf(direction, "N Click"); 
							if(pointerLocation == 85){ justOpened = 1; photoGallerySelected = 1; }
							else if(pointerLocation == 135) { justOpened = 1; mp3PlayerSelected = 1; }
							else if(pointerLocation == 185) { justOpened = 1; gameSelected = 1; }
						}
						else if(photoGallerySelected == 1)
						{
							switch(currentPic)
							{
								case 0:
									currentPic = 2;
									break;
								case 1:
									currentPic = 0;						
									break;
								case 2: 
									currentPic = 1;
									break;
							}
						}
						else if(gameSelected == 1)
						{
							moveUp = 1;
							if(!play){
								GLCD_DisplayString(1, 0, __FI, "                      ");
								GLCD_DisplayString(2, 0, __FI, "                      ");
								play = 1;
							}
							if(gameover){
								justOpened = 1;
							}
						}
						break;
					case KBD_RIGHT:
						sprintf(direction, "R"); 
						if(photoGallerySelected == 1)
						{
							switch(currentPic)
							{
								case 0:
									currentPic = 1;
									break;
								case 1:
									currentPic = 2;						
									break;
								case 2: 
									currentPic = 0;
									break;
							}
						}
						break;
					case KBD_UP + 0x01:
						sprintf(direction, "U Click"); 
						break;
					case KBD_DOWN + 0x01:
						sprintf(direction, "D Click"); 
						break;
					case KBD_LEFT + 0x01:
						sprintf(direction, "L Click");		
						if(photoGallerySelected == 1 || gameSelected == 1) 
						{ 
							photoGallerySelected = 0; 
							mp3PlayerSelected = 0; 
							gameSelected = 0; 
							menuSelected = 1;
							justOpened = 1;
						}		
						break;
					case KBD_RIGHT + 0x01:
						sprintf(direction, "R Click"); 
						break;
					default:
						sprintf(direction, "N"); 
						break;
				}
			
				
				if(photoGallerySelected == 1)
				{
					menuSelected = 0;
					gameSelected = 0;
					mp3PlayerSelected = 0;
					if(justOpened == 1)
					{
						justOpened = 0;
						GLCD_SetBackColor(Black);
						GLCD_SetTextColor(Yellow);
						GLCD_DisplayString(0, 0, __FI, "                      ");
						GLCD_DisplayString(1, 0, __FI, "                      ");
						GLCD_DisplayString(2, 0, __FI, "                      ");
						GLCD_DisplayString(3, 0, __FI, "                      ");
						GLCD_DisplayString(4, 0, __FI, "                      ");
						GLCD_DisplayString(5, 0, __FI, "                      ");
						GLCD_DisplayString(6, 0, __FI, "                      ");
						GLCD_DisplayString(7, 0, __FI, "                      ");
						GLCD_DisplayString(8, 0, __FI, "                      ");
						GLCD_DisplayString(9, 0, __FI, "                      ");
						GLCD_DisplayString(10, 0, __FI, "                      ");			
					}
					if(lastPic != currentPic)
					{
						lastPic = currentPic;
						GLCD_DisplayString(0, 0, __FI, "                      ");
						GLCD_DisplayString(1, 0, __FI, "                      ");
						GLCD_DisplayString(2, 0, __FI, "                      ");
						GLCD_DisplayString(3, 0, __FI, "                      ");
						GLCD_DisplayString(4, 0, __FI, "                      ");
						GLCD_DisplayString(5, 0, __FI, "                      ");
						GLCD_DisplayString(6, 0, __FI, "                      ");
						GLCD_DisplayString(7, 0, __FI, "                      ");
						GLCD_DisplayString(8, 0, __FI, "                      ");
						GLCD_DisplayString(9, 0, __FI, "                      ");
						GLCD_DisplayString(10, 0, __FI, "                      ");			
					}
					switch(currentPic)
					{
						case 0:
							GLCD_Bitmap(0, 150, 320, 80, foreground);		
							break;
						case 1:
							GLCD_Bitmap(0, 0, 300, 97, logo);  						
							break;
						case 2: 
							GLCD_Bitmap(25, pointerLocation, 34, 34, bird_mid); 
							break;
					}
				}
				else if(mp3PlayerSelected == 1)
				{
					menuSelected = 0;
					photoGallerySelected = 0;
					gameSelected = 0;
					
					if(justOpened == 1)
					{
						justOpened = 0;
						#ifdef __USE_LCD
								GLCD_SetBackColor(Black);
								GLCD_SetTextColor(Yellow);
								GLCD_DisplayString(0, 0, __FI, "      MP3 Player      ");
								GLCD_DisplayString(1, 0, __FI, "                      ");
								GLCD_DisplayString(2, 0, __FI, "                      ");
								GLCD_DisplayString(3, 0, __FI, "                      ");
								GLCD_DisplayString(4, 0, __FI, "    Now Streaming     ");
								GLCD_DisplayString(5, 0, __FI, "    Left To Quit.     ");
								GLCD_DisplayString(6, 0, __FI, "                      ");
								GLCD_DisplayString(7, 0, __FI, "                      ");
								GLCD_DisplayString(8, 0, __FI, "                      ");
								GLCD_DisplayString(9, 0, __FI, "                      ");
								GLCD_DisplayString(10, 0, __FI, "                      ");													
						#endif
						
						 /* SystemClockUpdate() updates the SystemFrequency variable */
						SystemClockUpdate();

						LPC_PINCON->PINSEL1 &=~((0x03<<18)|(0x03<<20));  
						/* P0.25, A0.0, function 01, P0.26 AOUT, function 10 */
						LPC_PINCON->PINSEL1 |= ((0x01<<18)|(0x02<<20));

						/* Enable CLOCK into ADC controller */
						LPC_SC->PCONP |= (1 << 12);

						LPC_ADC->CR = 0x00200E04;		/* ADC: 10-bit AIN2 @ 4MHz */
						LPC_DAC->CR = 0x00008000;		/* DAC Output set to Middle Point */

						/* By default, the PCLKSELx value is zero, thus, the PCLK for
						all the peripherals is 1/4 of the SystemFrequency. */
						/* Bit 2~3 is for TIMER0 */
						pclkdiv = (LPC_SC->PCLKSEL0 >> 2) & 0x03;
						switch ( pclkdiv )
						{
	
						case 0x00:
						default:
							pclk = SystemFrequency/4;
						break;
						case 0x01:
							pclk = SystemFrequency;
						break; 
						case 0x02:
							pclk = SystemFrequency/2;
						break; 
						case 0x03:
							pclk = SystemFrequency/8;
						break;
						}

						LPC_TIM0->MR0 = pclk/DATA_FREQ - 1;	/* TC0 Match Value 0 */
						LPC_TIM0->MCR = 3;					/* TCO Interrupt and Reset on MR0 */
						LPC_TIM0->TCR = 1;					/* TC0 Enable */
						NVIC_EnableIRQ(TIMER0_IRQn);

						USB_Init();				/* USB Initialization */
						USB_Connect(TRUE);		/* USB Connect */				
					}
				}
				else if(gameSelected == 1)
				{
					menuSelected = 0;
					photoGallerySelected = 0;
					mp3PlayerSelected = 0;
					if(justOpened == 1)
					{
						justOpened = 0;
						GLCD_SetBackColor(Black);
						GLCD_DisplayString(0, 0, __FI, "                      ");
						GLCD_DisplayString(1, 0, __FI, "   Select To Start    ");
						GLCD_DisplayString(2, 0, __FI, "    Left To Leave     ");
						GLCD_DisplayString(3, 0, __FI, "                      ");
						GLCD_DisplayString(4, 0, __FI, "                      ");
						GLCD_DisplayString(5, 0, __FI, "                      ");
						GLCD_DisplayString(6, 0, __FI, "                      ");
						GLCD_DisplayString(7, 0, __FI, "                      ");
						GLCD_DisplayString(8, 0, __FI, "                      ");
						GLCD_DisplayString(9, 0, __FI, "                      ");
						GLCD_DisplayString(10, 0, __FI, "                      ");
						
						gameover = 0;
						play = 0;
						dead = 0;
						gameCharX = 120;
						gameCharY = 120;
						pipeX = 240;
						pipeY = 0;
						pipeX2 = 80;
						pipeY2 = 0;
						score = 0;
						GLCD_Bitmap(120, 120, 34, 34, bird_mid); 
					}		

					if(play){	
						
						if(pipeX > 111 && pipeX < 150){
							if(activePipe == 1){
								if(gameCharY < 92 || (gameCharY+30) > 155) {
									dead = 1;
								}	
							}
							else if(activePipe == 2){
								if(gameCharY < 138 || (gameCharY+30) > 209) {
									dead = 1;
								}	
							}
							else if(activePipe == 3){
								if(gameCharY < 32 || (gameCharY+30) > 133) {
									dead = 1;
								}	
							}
						}
						else if(pipeX == 150){
							score++;
							sprintf(scoreText, "Score: %i", score);
							GLCD_DisplayString(0, 0, __FI,  (unsigned char *)scoreText);
						}
						
						if(pipeX2 > 111 && pipeX2 < 150){
							if(activePipe2 == 1){
								if(gameCharY < 92 || (gameCharY+30) > 155) {
									dead = 1;
								}	
							}
							else if(activePipe2 == 2){
								if(gameCharY < 138 || (gameCharY+30) > 209) {
									dead = 1;
								}	
							}
							else if(activePipe2 == 3){
								if(gameCharY < 32 || (gameCharY+30) > 133) {
									dead = 1;
								}
							}
						}
						else if(pipeX2 == 150){
							score++;
							sprintf(scoreText, "Score: %i", score);
							GLCD_DisplayString(0, 0, __FI,  (unsigned char *)scoreText);
						}
						
						if(!dead){

							if(pipeX > -39){
								pipeX = pipeX - 5;
							}
							else{
								pipeX = 320;
								if(activePipe == 3){
									activePipe = 1;
								}
								else{
									activePipe = activePipe + 1;
								}
							}
							if(activePipe == 1){
								GLCD_Bitmap(pipeX, pipeY, 39, 240, pipe1); 
							}
							else if(activePipe == 2){
									GLCD_Bitmap(pipeX, pipeY, 39, 240, pipe2); 
							}
							else if(activePipe == 3){
									GLCD_Bitmap(pipeX, pipeY, 39, 240, pipe3); 
							}
							
							if(pipeX2 > -39){
								pipeX2 = pipeX2 - 5;
							}
							else{
								pipeX2 = 320;
								if(activePipe2 == 3){
									activePipe2 = 1;
								}
								else{
									activePipe2 = activePipe2 + 1;
								}
							}						
							if(activePipe2 == 1){
									GLCD_Bitmap(pipeX2, pipeY2, 39, 240, pipe1); 
							}
							else if(activePipe2 == 2){
									GLCD_Bitmap(pipeX2, pipeY2, 39, 240, pipe2); 
							}
							else if(activePipe2 == 3){
									GLCD_Bitmap(pipeX2, pipeY2, 39, 240, pipe3); 
							}
						}
						
						if(dead == 1){
							if(gameCharY < 1){
								gameCharY++;
							}
						}
						
						if(moveUp && !dead){
							if(counter < 5){
								gameCharY = gameCharY - 7;
								GLCD_Bitmap(gameCharX, gameCharY, 34, 34, bird_up); 
								counter++;
								
								if(gameCharY < -5){
									gameCharY = -5;
									dead = 1;
								}
							}
							else{
								counter = 0;
								moveUp = 0;
							}
						}
						else{
							if(dead == 1){
								gameCharY = gameCharY + 1;
								GLCD_Bitmap(gameCharX, gameCharY, 34, 34, bird_dead); 
							}
							else{
								gameCharY = gameCharY + 7;
								GLCD_Bitmap(gameCharX, gameCharY, 34, 34, bird_down); 
							}
							
							if(gameCharY > 205){
								gameCharY = 205;
								dead = 1;
								gameover = 1;
							}		
						}
					}				
				}
				else if(menuSelected == 1)
				{					
						gameSelected = 0;
						photoGallerySelected = 0;
						mp3PlayerSelected = 0;	
					
					  if(justOpened == 1)
						{
							justOpened = 0;
							GLCD_SetBackColor(Black);
							GLCD_SetTextColor(Yellow);
							GLCD_DisplayString(0, 0, __FI, "     Media Center     ");
							GLCD_DisplayString(1, 0, __FI, "      Select One      ");
							GLCD_DisplayString(2, 0, __FI, "                      ");
							GLCD_DisplayString(3, 0, __FI, "                      ");
							GLCD_DisplayString(4, 0, __FI, "                      ");
							GLCD_DisplayString(5, 0, __FI, "                      ");
							GLCD_DisplayString(6, 0, __FI, "                      ");
							GLCD_DisplayString(7, 0, __FI, "                      ");
							GLCD_DisplayString(8, 0, __FI, "                      ");
							GLCD_DisplayString(9, 0, __FI, "                      ");
							GLCD_DisplayString(10, 0, __FI, "                      ");
							GLCD_DisplayString(4, 5, __FI, "Photo Gallery   ");
							GLCD_DisplayString(6, 5, __FI, "MP3 Player      ");
							GLCD_DisplayString(8, 5, __FI, "Game            ");
						}
						GLCD_DisplayString(3, 0, __FI, "     ");
						GLCD_DisplayString(4, 0, __FI, "     ");
						GLCD_DisplayString(5, 0, __FI, "     ");
						GLCD_DisplayString(6, 0, __FI, "     ");
						GLCD_DisplayString(7, 0, __FI, "     ");
						GLCD_DisplayString(8, 0, __FI, "     ");
						GLCD_DisplayString(9, 0, __FI, "     ");
						GLCD_DisplayString(10, 0, __FI, "     ");	
						//GLCD_Bitmap(0, 0, 320, 150, clouds);
						GLCD_Bitmap(25, pointerLocation, 34, 34, bird_mid); 
				}
			}
  }
}

/******************************************************************************
**                            End Of File
******************************************************************************/

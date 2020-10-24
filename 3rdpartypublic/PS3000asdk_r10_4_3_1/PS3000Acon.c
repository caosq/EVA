/**************************************************************************
*
* Filename:    PS3000Acon.c
*
* Copyright:   Pico Technology Limited 2010
*
* Author:      RPM
*
* Description:
*   This is a console mode program that demonstrates how to use the
*   PicoScope 3000a series API.
*
* Examples:
*    Collect a block of samples immediately
*    Collect a block of samples when a trigger event occurs
*    Collect a stream of data immediately
*    Collect a stream of data when a trigger event occurs
*    Set Signal Generator, using standard or custom signals
*    Change timebase & voltage scales
*    Display data in mV or ADC counts
*    Handle power source changes (PicoScope34xxA/B devices only)
*
*	To build this application:
*			 Set up a project for a 32-bit console mode application
*			 Add this file to the project
*			 Add PS3000A.lib to the project
*			 Add ps3000aApi.h and picoStatus.h to the project
*			 Build the project
*
***************************************************************************/
#include <stdio.h>

/* Headers for Windows */
#ifdef _WIN32
#include "windows.h"
#include <conio.h>
#include "..\ps3000aApi.h"
#else
#include <sys/types.h>
#include <string.h>

#include <libps3000a-1.0/ps3000aApi.h>
#include "linux_utils.h"
#endif

#define PREF4 __stdcall

int cycles = 0;

#define BUFFER_SIZE 	1024

#define QUAD_SCOPE		4
#define DUAL_SCOPE		2

typedef struct
{
	short DCcoupled;
	short range;
	short enabled;
}CHANNEL_SETTINGS;

typedef enum
{
	MODEL_NONE = 0,
	MODEL_PS3204A = 0xA204,
	MODEL_PS3204B = 0xB204,
	MODEL_PS3205A = 0xA205,
	MODEL_PS3205B = 0xB205,
	MODEL_PS3206A = 0xA206,
	MODEL_PS3206B = 0xB206,
	MODEL_PS3404A = 0xA404,
	MODEL_PS3404B = 0xB404,
	MODEL_PS3405A = 0xA405,
	MODEL_PS3405B = 0xB405,
	MODEL_PS3406A = 0xA406,
	MODEL_PS3406B = 0xB406,
} MODEL_TYPE;

typedef enum
{
	SIGGEN_NONE = 0,
	SIGGEN_FUNCTGEN = 1,
	SIGGEN_AWG = 2
} SIGGEN_TYPE;

typedef struct tTriggerDirections
{
	PS3000A_THRESHOLD_DIRECTION channelA;
	PS3000A_THRESHOLD_DIRECTION channelB;
	PS3000A_THRESHOLD_DIRECTION channelC;
	PS3000A_THRESHOLD_DIRECTION channelD;
	PS3000A_THRESHOLD_DIRECTION ext;
	PS3000A_THRESHOLD_DIRECTION aux;
}TRIGGER_DIRECTIONS;

typedef struct tPwq
{
	PS3000A_PWQ_CONDITIONS * conditions;
	short nConditions;
	PS3000A_THRESHOLD_DIRECTION direction;
	unsigned long lower;
	unsigned long upper;
	PS3000A_PULSE_WIDTH_TYPE type;
}PWQ;

typedef struct
{
	short handle;
	MODEL_TYPE				model;
	PS3000A_RANGE			firstRange;
	PS3000A_RANGE			lastRange;
	short							channelCount;
	short							maxValue;
	short							sigGen;
	short							ETS;
	short							AWGFileSize;

	CHANNEL_SETTINGS	channelSettings [PS3000A_MAX_CHANNELS];
}UNIT;

unsigned long timebase = 8;
short       oversample = 1;
BOOL      scaleVoltages = TRUE;

unsigned short inputRanges [PS3000A_MAX_RANGES] = {	10, 
													20,
													50,
													100,
													200,
													500,
													1000,
													2000,
													5000,
													10000,
													20000};

BOOL     			g_ready = FALSE;
long long 		g_times [PS3000A_MAX_CHANNELS];
short     		g_timeUnit;
long      		g_sampleCount;
unsigned long	g_startIndex;
short			g_autoStopped;
short			g_trig = 0;
unsigned long	g_trigAt = 0;

/****************************************************************************
* Callback
* used by PS3000A data streaimng collection calls, on receipt of data.
* used to set global flags etc checked by user routines
****************************************************************************/
void PREF4 CallBackStreaming(	short handle,
	long noOfSamples,
	unsigned long	startIndex,
	short overflow,
	unsigned long triggerAt,
	short triggered,
	short autoStop,
	void	*pParameter)
{
	// used for streaming
	g_sampleCount = noOfSamples;
	g_startIndex	= startIndex;
	g_autoStopped		= autoStop;

	// flag to say done reading data
	g_ready = TRUE;

	// flags to show if & where a trigger has occurred
	g_trig = triggered;
	g_trigAt = triggerAt;
}

/****************************************************************************
* Callback
* used by PS3000A data block collection calls, on receipt of data.
* used to set global flags etc checked by user routines
****************************************************************************/
void PREF4 CallBackBlock( short handle, PICO_STATUS status, void * pParameter)
{
	if (status != PICO_CANCELLED)
		g_ready = TRUE;
}

/****************************************************************************
* SetDefaults - restore default settings
****************************************************************************/
void SetDefaults(UNIT * unit)
{
	PICO_STATUS status;
	int i;

	status = ps3000aSetEts(unit->handle, PS3000A_ETS_OFF, 0, 0, NULL); // Turn off ETS

	for (i = 0; i < unit->channelCount; i++) // reset channels to most recent settings
	{
		status = ps3000aSetChannel(unit->handle, (PS3000A_CHANNEL)(PS3000A_CHANNEL_A + i),
			unit->channelSettings[PS3000A_CHANNEL_A + i].enabled,
			(PS3000A_COUPLING)unit->channelSettings[PS3000A_CHANNEL_A + i].DCcoupled,
			(PS3000A_RANGE)unit->channelSettings[PS3000A_CHANNEL_A + i].range, 0);
	}
}

/****************************************************************************
* adc_to_mv
*
* Convert an 16-bit ADC count into millivolts
****************************************************************************/
int adc_to_mv(long raw, int ch, UNIT * unit)
{
	return (raw * inputRanges[ch]) / unit->maxValue;
}

/****************************************************************************
* mv_to_adc
*
* Convert a millivolt value into a 16-bit ADC count
*
*  (useful for setting trigger thresholds)
****************************************************************************/
short mv_to_adc(short mv, short ch, UNIT * unit)
{
	return (mv * unit->maxValue) / inputRanges[ch];
}

/****************************************************************************************
* ChangePowerSource - function to handle switches between +5V supply, and USB only power
* Only applies to ps34xxA/B units 
******************************************************************************************/
PICO_STATUS ChangePowerSource(short handle, PICO_STATUS status)
{
	char ch;

	switch (status)
	{
		case PICO_POWER_SUPPLY_NOT_CONNECTED:			// User must acknowledge they want to power via usb
			do
			{
				printf("\n5V power supply not connected");
				printf("\nDo you want to run using USB only Y/N?\n");
				ch = toupper(_getch());
				if(ch == 'Y')
				{
					printf("\nPowering the unit via USB\n");
						status = ps3000aChangePowerSource(handle, PICO_POWER_SUPPLY_NOT_CONNECTED);		// Tell the driver that's ok
						if (status == PICO_POWER_SUPPLY_UNDERVOLTAGE)
							status = ChangePowerSource(handle, status);
				}
			}
			while(ch != 'Y' && ch != 'N');
			printf(ch == 'N'?"Please use the +5V power supply to power this unit\n":"");
		break;

		case PICO_POWER_SUPPLY_CONNECTED:
			printf("\nUsing +5V power supply voltage\n");
			status = ps3000aChangePowerSource(handle, PICO_POWER_SUPPLY_CONNECTED);					// Tell the driver we are powered from +5V supply
			break;

		case PICO_POWER_SUPPLY_UNDERVOLTAGE:
			do
			{
				printf("\nUSB not supplying required voltage");
				printf("\nPlease plug in the +5V power supply\n");
				printf("\nHit any key to continue, or Esc to exit...\n");
				ch = _getch();
				if (ch == 0x1B)	// ESC key
					exit(0);
				else
						status = ps3000aChangePowerSource(handle, PICO_POWER_SUPPLY_CONNECTED);		// Tell the driver that's ok
			}
			while (status == PICO_POWER_SUPPLY_REQUEST_INVALID);
			break;
	}
	return status;
}

/****************************************************************************
* BlockDataHandler
* - Used by all block data routines
* - acquires data (user sets trigger mode before calling), displays 10 items
*   and saves all to block.txt
* Input :
* - unit : the unit to use.
* - text : the text to display before the display of data slice
* - offset : the offset into the data buffer to start the display's slice.
****************************************************************************/
void BlockDataHandler(UNIT * unit, char * text, int offset)
{
	int i, j;
	long timeInterval;
	long sampleCount= BUFFER_SIZE;
	FILE * fp = NULL;
	long maxSamples;
	short * buffers[PS3000A_MAX_CHANNEL_BUFFERS];
	long timeIndisposed;
	PICO_STATUS status;
	short retry;

	for (i = 0; i < unit->channelCount; i++) 
	{
		buffers[i * 2] = (short*)malloc(sampleCount * sizeof(short));
		buffers[i * 2 + 1] = (short*)malloc(sampleCount * sizeof(short));
		status = ps3000aSetDataBuffers(unit->handle, (PS3000A_CHANNEL)i, buffers[i * 2], buffers[i * 2 + 1], sampleCount, 0, PS3000A_RATIO_MODE_NONE);

		printf("BlockDataHandler:ps3000aSetDataBuffers(channel %d) ------ 0x%08lx \n", i, status);
	}

	/*  find the maximum number of samples, the time interval (in timeUnits),
	*		 the most suitable time units, and the maximum oversample at the current timebase*/
	while (ps3000aGetTimebase(unit->handle, timebase, sampleCount, &timeInterval, oversample, &maxSamples, 0))
	{
		timebase++;
	}

	printf("\nTimebase: %lu  SampleInterval: %ldnS  oversample: %hd\n", timebase, timeInterval, oversample);

	/* Start it collecting, then wait for completion*/
	g_ready = FALSE;
	

	do
	{
		retry = 0;
		if((status = ps3000aRunBlock(unit->handle, 0, sampleCount, timebase, oversample,	&timeIndisposed, 0, CallBackBlock, NULL)) != PICO_OK)
		{
			if(status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED || status == PICO_POWER_SUPPLY_UNDERVOLTAGE)       // 34xxA/B devices...+5V PSU connected or removed
			{
				status = ChangePowerSource(unit->handle, status);
				retry = 1;
			}
			else
				printf("BlockDataHandler:ps3000aRunBlock ------ 0x%08lx \n", status);
		}
	}
	while(retry);

	printf("Waiting for trigger...Press a key to abort\n");

	while (!g_ready && !_kbhit())
	{
		Sleep(0);
	}


	if(g_ready) 
	{
		if((status = ps3000aGetValues(unit->handle, 0, (unsigned long*) &sampleCount, 1, PS3000A_RATIO_MODE_NONE, 0, NULL)) != PICO_OK)
			if(status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED || status == PICO_POWER_SUPPLY_UNDERVOLTAGE)
			{
				if (status == PICO_POWER_SUPPLY_UNDERVOLTAGE)
					ChangePowerSource(unit->handle, status);
				else
				printf("\nPower Source Changed. Data collection aborted.\n");
			}
			else
				printf("BlockDataHandler:ps3000aGetValues ------ 0x%08lx \n", status);
		else
		{
			/* Print out the first 10 readings, converting the readings to mV if required */
			printf("%s\n",text);
			printf("Value (%s)\n", ( scaleVoltages ) ? ("mV") : ("ADC Counts"));

			for (j = 0; j < unit->channelCount; j++) 
			{
				printf("Channel%c:    ", 'A' + j);
			}
			printf("\n");

			for (i = offset; i < offset+10; i++) 
			{
				for (j = 0; j < unit->channelCount; j++) 
				{
					if (unit->channelSettings[j].enabled) 
					{
						printf("  %6d     ", scaleVoltages ? 
							adc_to_mv(buffers[j * 2][i], unit->channelSettings[PS3000A_CHANNEL_A + j].range, unit)	// If scaleVoltages, print mV value
							: buffers[j * 2][i]);																	// else print ADC Count
					}
				}
				printf("\n");
			}

			sampleCount = min(sampleCount, BUFFER_SIZE);

			fopen_s(&fp, "block.txt", "w");
			if (fp != NULL)
			{
				fprintf(fp, "Block Data log\n\n");
				fprintf(fp,"Results shown for each of the %d Channels are......\n",unit->channelCount);
				fprintf(fp,"Maximum Aggregated value ADC Count & mV, Minimum Aggregated value ADC Count & mV\n\n");

				fprintf(fp, "Time  ");
				for (i = 0; i < unit->channelCount; i++) 
					fprintf(fp," Ch   Max ADC   Max mV   Min ADC   Min mV   ");
				fprintf(fp, "\n");

				for (i = 0; i < sampleCount; i++) 
				{
					fprintf(fp, "%5lld ", g_times[0] + (long long)(i * timeInterval));
					for (j = 0; j < unit->channelCount; j++) 
					{
						if (unit->channelSettings[j].enabled) 
						{
							fprintf(	fp,
								"Ch%C  %6d = %+6dmV, %6d = %+6dmV   ",
								'A' + j,
								buffers[j * 2][i],
								adc_to_mv(buffers[j * 2][i], unit->channelSettings[PS3000A_CHANNEL_A + j].range, unit),
								buffers[j * 2 + 1][i],
								adc_to_mv(buffers[j * 2 + 1][i], unit->channelSettings[PS3000A_CHANNEL_A + j].range, unit));
						}
					}
					fprintf(fp, "\n");
				}
			}
			else
				printf(	"Cannot open the file block.txt for writing.\n"
				"Please ensure that you have permission to access.\n");
		} 
	}
	else 
	{
		printf("data collection aborted\n");
		_getch();
	}

	if ((status = ps3000aStop(unit->handle)) != PICO_OK)
		printf("BlockDataHandler:ps3000aStop ------ 0x%08lx \n", status);

	if (fp != NULL)
		fclose(fp);

	for (i = 0; i < unit->channelCount * 2; i++) 
	{
		free(buffers[i]);
	}
}

/****************************************************************************
* Stream Data Handler
* - Used by the two stream data examples - untriggered and triggered
* Inputs:
* - unit - the unit to sample on
* - preTrigger - the number of samples in the pre-trigger phase 
*					(0 if no trigger has been set)
***************************************************************************/
void StreamDataHandler(UNIT * unit, unsigned long preTrigger)
{
	long i, j;
	unsigned long sampleCount= BUFFER_SIZE * 10; /*  *10 is to make sure buffer large enough */
	FILE * fp = NULL;
	short * buffers[PS3000A_MAX_CHANNEL_BUFFERS];
	PICO_STATUS status;
	unsigned long sampleInterval = 1;
	int index = 0;
	int totalSamples;
	unsigned long triggeredAt = 0;
	short retry = 0;
	short powerChange = 0;

	for (i = 0; i < unit->channelCount; i++) // create data buffers
	{
		buffers[i * 2] = (short*) malloc(sampleCount * sizeof(short));
		buffers[i * 2 + 1] = (short*)malloc(sampleCount * sizeof(short));
		status = ps3000aSetDataBuffers(	unit->handle, 
										(PS3000A_CHANNEL)i, 
										buffers[i * 2],
										buffers[i * 2 + 1], 
										sampleCount, 
										0,
										PS3000A_RATIO_MODE_AGGREGATE);
	}

	printf("Waiting for trigger...Press a key to abort\n");
	g_autoStopped = FALSE;

	
	do
	{
		retry = 0;
		if((status = ps3000aRunStreaming(unit->handle, 
								&sampleInterval, 
								PS3000A_US,
								preTrigger, 
								1000000 - preTrigger, 
								//FALSE,
								TRUE,
								1000,
								PS3000A_RATIO_MODE_AGGREGATE,
								sampleCount)) != PICO_OK)
		{
			if(status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED || PICO_POWER_SUPPLY_UNDERVOLTAGE)
			{
				status = ChangePowerSource(unit->handle, status);
				retry = 1;
			}
			else
				printf("BlockDataHandler:ps3000aRunStreaming ------ 0x%08lx \n", status);
		}
	}
	while(retry);


	printf("Streaming data...Press a key to abort\n");

	fopen_s(&fp, "stream.txt", "w");

	if (fp != NULL)
	{
		fprintf(fp,"For each of the %d Channels, results shown are....\n",unit->channelCount);
		fprintf(fp,"Maximum Aggregated value ADC Count & mV, Minimum Aggregated value ADC Count & mV\n\n");

		for (i = 0; i < unit->channelCount; i++) 
			fprintf(fp,"Ch  Max ADC    Max mV  Min ADC    Min mV   ");
		fprintf(fp, "\n");
	}

	totalSamples = 0;
	while (!_kbhit() && !g_autoStopped && !powerChange)
	{
		/* Poll until data is received. Until then, GetStreamingLatestValues wont call the callback */
		Sleep(100);
		g_ready = FALSE;

		status = ps3000aGetStreamingLatestValues(unit->handle, CallBackStreaming, NULL);
		if(status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED || status == PICO_POWER_SUPPLY_UNDERVOLTAGE) // 34xxA/B devices...+5V PSU connected or removed
		{
			if (status == PICO_POWER_SUPPLY_UNDERVOLTAGE)
				ChangePowerSource(unit->handle, status);
			printf("\n\nPower Source Change");
			powerChange = 1;
		}

		index ++;

		if (g_ready && g_sampleCount > 0) /* can be ready and have no data, if autoStop has fired */
		{
			if (g_trig)
				triggeredAt = totalSamples + g_trigAt;
			totalSamples += g_sampleCount;
			printf("\nCollected %li samples, index = %lu, Total: %d samples  ", g_sampleCount, g_startIndex, totalSamples);

			if (g_trig)
				printf("Trig. at index %lu", triggeredAt);

			if (fp != NULL)
			{
				for (i = g_startIndex; i < (long)(g_startIndex + g_sampleCount); i++) 
				{
					for (j = 0; j < unit->channelCount; j++) 
					{
						if (unit->channelSettings[j].enabled) 
						{
							fprintf(	fp,
								"Ch%C %6d = %+6dmV, %6d = %+6dmV   ",
								(char)('A' + j),
								buffers[j * 2][i],
								adc_to_mv(buffers[j * 2][i], unit->channelSettings[PS3000A_CHANNEL_A + j].range, unit),
								buffers[j * 2 + 1][i],
								adc_to_mv(buffers[j * 2 + 1][i], unit->channelSettings[PS3000A_CHANNEL_A + j].range, unit));
						}
					}
					fprintf(fp, "\n");
				}
			}
			else
				printf("Cannot open the file stream.txt for writing.\n");
		}
	}

	if(fp != NULL) fclose(fp);	

	ps3000aStop(unit->handle);

	if (!g_autoStopped && !powerChange) 
	{
		printf("\ndata collection aborted\n");
		_getch();
	}
	for (i = 0; i < unit->channelCount * 2; i++) 
	{
		free(buffers[i]);
	}
}


PICO_STATUS SetTrigger(	short handle,
						struct tPS3000ATriggerChannelProperties * channelProperties,
						short nChannelProperties,
						struct tPS3000ATriggerConditions * triggerConditions,
						short nTriggerConditions,
						TRIGGER_DIRECTIONS * directions,
						struct tPwq * pwq,
						unsigned long delay,
						short auxOutputEnabled,
						long autoTriggerMs)
{
	PICO_STATUS status;

	if ((status = ps3000aSetTriggerChannelProperties(handle,
													channelProperties,
													nChannelProperties,
													auxOutputEnabled,
													autoTriggerMs)) != PICO_OK) 
	{
		printf("SetTrigger:ps3000aSetTriggerChannelProperties ------ Ox%8lx \n", status);
		return status;
	}

	if ((status = ps3000aSetTriggerChannelConditions(handle, triggerConditions, nTriggerConditions)) != PICO_OK) 
	{
		printf("SetTrigger:ps3000aSetTriggerChannelConditions ------ 0x%8lx \n", status);
		return status;
	}

	if ((status = ps3000aSetTriggerChannelDirections(handle,
				directions->channelA,
				directions->channelB,
				directions->channelC,
				directions->channelD,
				directions->ext,
				directions->aux)) != PICO_OK) 
	{
		printf("SetTrigger:ps3000aSetTriggerChannelDirections ------ 0x%08lx \n", status);
		return status;
	}

	if ((status = ps3000aSetTriggerDelay(handle, delay)) != PICO_OK) 
	{
		printf("SetTrigger:ps3000aSetTriggerDelay ------ 0x%08lx \n", status);
		return status;
	}

	if((status = ps3000aSetPulseWidthQualifier(handle, 
				pwq->conditions,
				pwq->nConditions, 
				pwq->direction,
				pwq->lower, 
				pwq->upper, 
				pwq->type)) != PICO_OK)
	{
		printf("SetTrigger:ps3000aSetPulseWidthQualifier ------ 0x%08lx \n", status);
		return status;
	}

	return status;
}

/****************************************************************************
* CollectBlockImmediate
*  this function demonstrates how to collect a single block of data
*  from the unit (start collecting immediately)
****************************************************************************/
void CollectBlockImmediate(UNIT * unit)
{
	struct tPwq pulseWidth;
	struct tTriggerDirections directions;

	memset(&directions, 0, sizeof(struct tTriggerDirections));
	memset(&pulseWidth, 0, sizeof(struct tPwq));

	printf("Collect block immediate...\n");
	printf("Press a key to start\n");
	_getch();

	SetDefaults(unit);

	/* Trigger disabled	*/
	SetTrigger(unit->handle, NULL, 0, NULL, 0, &directions, &pulseWidth, 0, 0, 0);

	BlockDataHandler(unit, "First 10 readings\n", 0);
}

/****************************************************************************
* CollectBlockEts
*  this function demonstrates how to collect a block of
*  data using equivalent time sampling (ETS).
****************************************************************************/
void CollectBlockEts(UNIT * unit)
{
	PICO_STATUS status;
	long ets_sampletime;
	short	triggerVoltage = mv_to_adc(1000,	unit->channelSettings[PS3000A_CHANNEL_A].range, unit);
	unsigned long delay = 0;
	struct tPwq pulseWidth;
	struct tTriggerDirections directions;

	struct tPS3000ATriggerChannelProperties sourceDetails = {	triggerVoltage,
																256 * 10,
																triggerVoltage,
																256 * 10,
																PS3000A_CHANNEL_A,
																PS3000A_LEVEL };

	struct tPS3000ATriggerConditions conditions = {	PS3000A_CONDITION_TRUE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE };



	memset(&pulseWidth, 0, sizeof(struct tPwq));
	memset(&directions, 0, sizeof(struct tTriggerDirections));
	directions.channelA = PS3000A_RISING;

	printf("Collect ETS block...\n");
	printf("Collects when value rises past %d", scaleVoltages? 
		adc_to_mv(sourceDetails.thresholdUpper,	unit->channelSettings[PS3000A_CHANNEL_A].range, unit)	// If scaleVoltages, print mV value
		: sourceDetails.thresholdUpper);																// else print ADC Count
	printf(scaleVoltages? "mV\n" : "ADC Counts\n");
	printf("Press a key to start...\n");
	_getch();

	SetDefaults(unit);

	//Trigger enabled
	//Rising edge
	//Threshold = 1000mV
	status = SetTrigger(unit->handle, &sourceDetails, 1, &conditions, 1, &directions, &pulseWidth, delay, 0, 0);

	status = ps3000aSetEts(unit->handle, PS3000A_ETS_FAST, 20, 4, &ets_sampletime);
	printf("ETS Sample Time is: %ld\n", ets_sampletime);

	BlockDataHandler(unit, "Ten readings after trigger\n", BUFFER_SIZE / 10 - 5); // 10% of data is pre-trigger
}

/****************************************************************************
* CollectBlockTriggered
*  this function demonstrates how to collect a single block of data from the
*  unit, when a trigger event occurs.
****************************************************************************/
void CollectBlockTriggered(UNIT * unit)
{
	short	triggerVoltage = mv_to_adc(1000, unit->channelSettings[PS3000A_CHANNEL_A].range, unit);

	struct tPS3000ATriggerChannelProperties sourceDetails = {	triggerVoltage,
																256 * 10,
																triggerVoltage,
																256 * 10,
																PS3000A_CHANNEL_A,
																PS3000A_LEVEL};

	struct tPS3000ATriggerConditions conditions = {	PS3000A_CONDITION_TRUE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE};

	struct tPwq pulseWidth;

	struct tTriggerDirections directions = {	PS3000A_RISING,
												PS3000A_NONE,
												PS3000A_NONE,
												PS3000A_NONE,
												PS3000A_NONE,
												PS3000A_NONE };

	memset(&pulseWidth, 0, sizeof(struct tPwq));

	printf("Collect block triggered...\n");
	printf("Collects when value rises past %d", scaleVoltages?
		adc_to_mv(sourceDetails.thresholdUpper, unit->channelSettings[PS3000A_CHANNEL_A].range, unit)	// If scaleVoltages, print mV value
		: sourceDetails.thresholdUpper);																// else print ADC Count
	printf(scaleVoltages?"mV\n" : "ADC Counts\n");

	printf("Press a key to start...\n");
	_getch();

	SetDefaults(unit);

	/* Trigger enabled
	* Rising edge
	* Threshold = 1000mV */
	SetTrigger(unit->handle, &sourceDetails, 1, &conditions, 1, &directions, &pulseWidth, 0, 0, 0);

	BlockDataHandler(unit, "Ten readings after trigger\n", 0);
}

/****************************************************************************
* CollectRapidBlock
*  this function demonstrates how to collect a set of captures using 
*  rapid block mode.
****************************************************************************/
void CollectRapidBlock(UNIT * unit)
{
	unsigned short nCaptures;
	long nMaxSamples;
	unsigned long nSamples = 1000;
	long timeIndisposed;
	short capture, channel;
	short ***rapidBuffers;
	short *overflow;
	PICO_STATUS status;
	short i;
	unsigned long nCompletedCaptures;
	short retry;

	short	triggerVoltage = mv_to_adc(1000, unit->channelSettings[PS3000A_CHANNEL_A].range, unit);

	struct tPS3000ATriggerChannelProperties sourceDetails = {	triggerVoltage,
																256 * 10,
																triggerVoltage,
																256 * 10,
																PS3000A_CHANNEL_A,
																PS3000A_LEVEL};

	struct tPS3000ATriggerConditions conditions = {	PS3000A_CONDITION_TRUE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE};

	struct tPwq pulseWidth;

	struct tTriggerDirections directions = {	PS3000A_RISING,
												PS3000A_NONE,
												PS3000A_NONE,
												PS3000A_NONE,
												PS3000A_NONE,
												PS3000A_NONE };

	memset(&pulseWidth, 0, sizeof(struct tPwq));

	printf("Collect rapid block triggered...\n");
	printf("Collects when value rises past %d",	scaleVoltages?
		adc_to_mv(sourceDetails.thresholdUpper, unit->channelSettings[PS3000A_CHANNEL_A].range, unit)	// If scaleVoltages, print mV value
		: sourceDetails.thresholdUpper);																// else print ADC Count
	printf(scaleVoltages?"mV\n" : "ADC Counts\n");
	printf("Press any key to abort\n");

	SetDefaults(unit);

	// Trigger enabled
	SetTrigger(unit->handle, &sourceDetails, 1, &conditions, 1, &directions, &pulseWidth, 0, 0, 0);

	//Set the number of captures
	nCaptures = 10;

	//Segment the memory
	status = ps3000aMemorySegments(unit->handle, nCaptures, &nMaxSamples);

	//Set the number of captures
	status = ps3000aSetNoOfCaptures(unit->handle, nCaptures);

	//Run
	timebase = 160;		//1 MS/s

	do
	{
		retry = 0;
		if((status = ps3000aRunBlock(unit->handle, 0, nSamples, timebase, 1, &timeIndisposed, 0, CallBackBlock, NULL)) != PICO_OK)
		{
			if(status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED)
			{
				status = ChangePowerSource(unit->handle, status);
				retry = 1;
			}
			else
				printf("BlockDataHandler:ps3000aRunBlock ------ 0x%08lx \n", status);
		}
	}
	while(retry);

	//Wait until data ready
	g_ready = 0;
	while(!g_ready && !_kbhit())
	{
		Sleep(0);
	}

	if(!g_ready)
	{
		_getch();
		status = ps3000aStop(unit->handle);
		status = ps3000aGetNoOfCaptures(unit->handle, &nCompletedCaptures);
		printf("Rapid capture aborted. %lu complete blocks were captured\n", nCompletedCaptures);
		printf("\nPress any key...\n\n");
		_getch();

		if(nCompletedCaptures == 0)
			return;

		//Only display the blocks that were captured
		nCaptures = (unsigned short)nCompletedCaptures;
	}

	//Allocate memory
	rapidBuffers = (short ***) calloc(unit->channelCount, sizeof(short*));
	overflow = (short *) calloc(unit->channelCount * nCaptures, sizeof(short));

	for (channel = 0; channel < unit->channelCount; channel++) 
	{
		rapidBuffers[channel] = (short **) calloc(nCaptures, sizeof(short*));
	}

	for (channel = 0; channel < unit->channelCount; channel++) 
	{	
		if(unit->channelSettings[channel].enabled)
		{
			for (capture = 0; capture < nCaptures; capture++) 
			{
				rapidBuffers[channel][capture] = (short *) calloc(nSamples, sizeof(short));
			}
		}
	}

	for (channel = 0; channel < unit->channelCount; channel++) 
	{
		if(unit->channelSettings[channel].enabled)
		{
			for (capture = 0; capture < nCaptures; capture++) 
			{
				status = ps3000aSetDataBuffer(unit->handle, (PS3000A_CHANNEL)channel, rapidBuffers[channel][capture], nSamples, capture, PS3000A_RATIO_MODE_NONE);
			}
		}
	}

	//Get data
	status = ps3000aGetValuesBulk(unit->handle, &nSamples, 0, nCaptures - 1, 1, PS3000A_RATIO_MODE_NONE, overflow);
	if (status == PICO_POWER_SUPPLY_CONNECTED || status == PICO_POWER_SUPPLY_NOT_CONNECTED)
		printf("\nPower Source Changed. Data collection aborted.\n");

	if (status == PICO_OK)
	{
		//print first 10 samples from each capture
		for (capture = 0; capture < nCaptures; capture++)
		{
			printf("\nCapture %d\n", capture + 1);
			for (channel = 0; channel < unit->channelCount; channel++) 
			{
				printf("Channel %c:\t", 'A' + channel);
			}
			printf("\n");

			for(i = 0; i < 10; i++)
			{
				for (channel = 0; channel < unit->channelCount; channel++) 
				{
					if(unit->channelSettings[channel].enabled)
					{
						printf("   %6d       ", scaleVoltages ? 
							adc_to_mv(rapidBuffers[channel][capture][i], unit->channelSettings[PS3000A_CHANNEL_A +channel].range, unit)	// If scaleVoltages, print mV value
							: rapidBuffers[channel][capture][i]);																	// else print ADC Count
					}
				}
				printf("\n");
			}
		}
	}

	//Stop
	status = ps3000aStop(unit->handle);

	//Free memory
	free(overflow);

	for (channel = 0; channel < unit->channelCount; channel++) 
	{	
		if(unit->channelSettings[channel].enabled)
		{
			for (capture = 0; capture < nCaptures; capture++) 
			{
				free(rapidBuffers[channel][capture]);
			}
		}
	}

	for (channel = 0; channel < unit->channelCount; channel++) 
	{
		free(rapidBuffers[channel]);
	}
	free(rapidBuffers);
}

/****************************************************************************
* Initialise unit' structure with Variant specific defaults
****************************************************************************/
void get_info(UNIT * unit)
{
	char description [6][25]= { "Driver Version",
		"USB Version",
		"Hardware Version",
		"Variant Info",
		"Serial",
		"Error Code" };
	short i, r = 0;
	char line [80];
	int variant;
	PICO_STATUS status = PICO_OK;

	if (unit->handle) 
	{
		for (i = 0; i < 5; i++) 
		{
			status = ps3000aGetUnitInfo(unit->handle, line, sizeof (line), &r, i);
			if (i == 3) 
			{
				variant = atoi(line);
				//To identify varians.....
				line[4] = toupper(line[4]);
				if (strlen(line) == 5)										// A or B variant unit 
				{
					if (line[1] == '2' && line[4] == 'A')		// i.e 3204A -> 0xA204
						variant += 0x9580;
					else
					if (line[1] == '2' && line[4] == 'B')		//i.e 3204B -> 0xB204
						variant +=0xA580;
					else
					if (line[1] == '4' && line[4] == 'A')		// i.e 3404A -> 0xA404
							variant += 0x96B8;
					else
					if (line[1] == '4' && line[4] == 'B')		//i.e 3404B -> 0xB404
						variant +=0xA6B8;
				}
			}
			printf("%s: %s\n", description[i], line);
		}

		switch (variant)
		{
			case MODEL_PS3204A:
				unit->model					= MODEL_PS3204A;
				unit->sigGen				= SIGGEN_FUNCTGEN;
				unit->firstRange		= PS3000A_50MV;
				unit->lastRange			= PS3000A_20V;
				unit->channelCount	= DUAL_SCOPE;
				unit->ETS						= FALSE;
				unit->AWGFileSize		= MIN_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS3204B:
				unit->model					= MODEL_PS3204B;
				unit->sigGen				= SIGGEN_AWG;
				unit->firstRange		= PS3000A_50MV;
				unit->lastRange			= PS3000A_20V;
				unit->channelCount	= DUAL_SCOPE;
				unit->ETS						= FALSE;
				unit->AWGFileSize		= MAX_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS3205A:
				unit->model					= MODEL_PS3205A;
				unit->sigGen				= SIGGEN_FUNCTGEN;
				unit->firstRange		= PS3000A_50MV;
				unit->lastRange			= PS3000A_20V;
				unit->channelCount	= DUAL_SCOPE;
				unit->ETS						= TRUE;
				unit->AWGFileSize		= MIN_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS3205B:
				unit->model					= MODEL_PS3205B;
				unit->sigGen				= SIGGEN_AWG;
				unit->firstRange		= PS3000A_50MV;
				unit->lastRange			= PS3000A_20V;
				unit->channelCount	= DUAL_SCOPE;
				unit->ETS						= TRUE;
				unit->AWGFileSize		= MAX_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS3206A:
				unit->model					= MODEL_PS3206A;
				unit->sigGen				= SIGGEN_FUNCTGEN;
				unit->firstRange		= PS3000A_50MV;
				unit->lastRange			= PS3000A_20V;
				unit->channelCount	= DUAL_SCOPE;
				unit->ETS						= TRUE;
				unit->AWGFileSize		= MIN_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS3206B:
				unit->model					= MODEL_PS3206B;
				unit->sigGen				= SIGGEN_AWG;
				unit->firstRange		= PS3000A_50MV;
				unit->lastRange			= PS3000A_20V;
				unit->channelCount	= DUAL_SCOPE;
				unit->ETS						= TRUE;
				unit->AWGFileSize		= PS3206B_MAX_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS3404A:
				unit->model					= MODEL_PS3404A;
				unit->sigGen				= SIGGEN_FUNCTGEN;
				unit->firstRange		= PS3000A_50MV;
				unit->lastRange			= PS3000A_20V;
				unit->channelCount	= QUAD_SCOPE;
				unit->ETS						= FALSE;
				unit->AWGFileSize		= MIN_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS3404B:
				unit->model					= MODEL_PS3404B;
				unit->sigGen				= SIGGEN_AWG;
				unit->firstRange		= PS3000A_50MV;
				unit->lastRange			= PS3000A_20V;
				unit->channelCount	= QUAD_SCOPE;
				unit->ETS						= FALSE;
				unit->AWGFileSize		= MAX_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS3405A:
				unit->model					= MODEL_PS3405A;
				unit->sigGen				= SIGGEN_FUNCTGEN;
				unit->firstRange		= PS3000A_50MV;
				unit->lastRange			= PS3000A_20V;
				unit->channelCount	= QUAD_SCOPE;
				unit->ETS						= TRUE;
				unit->AWGFileSize		= MIN_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS3405B:
				unit->model					= MODEL_PS3405B;
				unit->sigGen				= SIGGEN_AWG;
				unit->firstRange		= PS3000A_50MV;
				unit->lastRange			= PS3000A_20V;
				unit->channelCount	= QUAD_SCOPE;
				unit->ETS						= TRUE;
				unit->AWGFileSize		= MAX_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS3406A:
				unit->model					= MODEL_PS3406A;
				unit->sigGen				= SIGGEN_FUNCTGEN;
				unit->firstRange		= PS3000A_50MV;
				unit->lastRange			= PS3000A_20V;
				unit->channelCount	= QUAD_SCOPE;
				unit->ETS						= TRUE;
				unit->AWGFileSize		= MIN_SIG_GEN_BUFFER_SIZE;
				break;

			case MODEL_PS3406B:
				unit->model					= MODEL_PS3406B;
				unit->sigGen				= SIGGEN_AWG;
				unit->firstRange		= PS3000A_50MV;
				unit->lastRange			= PS3000A_20V;
				unit->channelCount	= QUAD_SCOPE;
				unit->ETS						= TRUE;
				unit->AWGFileSize		= PS3206B_MAX_SIG_GEN_BUFFER_SIZE;
				break;
		}
	}
}

/****************************************************************************
* Select input voltage ranges for channels
****************************************************************************/
void SetVoltages(UNIT * unit)
{
	int i, ch;
	int count = 0;

	/* See what ranges are available... */
	for (i = unit->firstRange; i <= unit->lastRange; i++) 
	{
		printf("%d -> %d mV\n", i, inputRanges[i]);
	}

	do
	{
		/* Ask the user to select a range */
		printf("Specify voltage range (%d..%d)\n", unit->firstRange, unit->lastRange);
		printf("99 - switches channel off\n");
		for (ch = 0; ch < unit->channelCount; ch++) 
		{
			printf("\n");
			do 
			{
				printf("Channel %c: ", 'A' + ch);
				fflush(stdin);
				scanf_s("%hd", &unit->channelSettings[ch].range);
			} while (unit->channelSettings[ch].range != 99 && (unit->channelSettings[ch].range < unit->firstRange || unit->channelSettings[ch].range > unit->lastRange));

			if (unit->channelSettings[ch].range != 99) 
			{
				printf(" - %d mV\n", inputRanges[unit->channelSettings[ch].range]);
				unit->channelSettings[ch].enabled = TRUE;
				count++;
			} 
			else 
			{
				printf("Channel Switched off\n");
				unit->channelSettings[ch].enabled = FALSE;
			}
		}
		printf(count == 0? "\n** At least 1 channel must be enabled **\n\n":"");
	}
	while(count == 0);	// must have at least one channel enabled

	SetDefaults(unit);	// Put these changes into effect
}

/****************************************************************************
*
* Select timebase, set oversample to on and time units as nano seconds
*
****************************************************************************/
void SetTimebase(UNIT unit)
{
	long timeInterval;
	long maxSamples;

	printf("Specify desired timebase: ");
	fflush(stdin);
	scanf_s("%lud", &timebase);

	while (ps3000aGetTimebase(unit.handle, timebase, BUFFER_SIZE, &timeInterval, 1, &maxSamples, 0))
	{
		timebase++;  // Increase timebase if the one specified can't be used. 
	}

	printf("Timebase used %lu = %ldns Sample Interval\n", timebase, timeInterval);
	oversample = TRUE;
}

/****************************************************************************
* Sets the signal generator
* - allows user to set frequency and waveform
* - allows for custom waveform (values -32768..32767) 
* - of up to 8192 samples long (PS3x04B & PS3x05B) 16384 samples long (PS3x06B)
******************************************************************************/
void SetSignalGenerator(UNIT unit)
{
	PICO_STATUS status;
	short waveform;
	unsigned long frequency = 1;
	char fileName [128];
	FILE * fp = NULL;
	short *arbitraryWaveform;
	long waveformSize = 0;
	unsigned long pkpk = 4000000;
	long offset = 0;
	char ch;
	short choice;
	double delta;


	while (_kbhit())			// use up keypress
		_getch();

	do
	{
		printf("\nSignal Generator\n================\n");
		printf("0 - SINE         1 - SQUARE\n");
		printf("2 - TRIANGLE     3 - DC VOLTAGE\n");
		if(unit.sigGen == SIGGEN_AWG)
		{
			printf("4 - RAMP UP      5 - RAMP DOWN\n");
			printf("6 - SINC         7 - GAUSSIAN\n");
			printf("8 - HALF SINE    A - AWG WAVEFORM\n");
		}
		printf("F - SigGen Off\n\n");

		ch = _getch();

		if (ch >= '0' && ch <='9')
			choice = ch -'0';
		else
			ch = toupper(ch);
	}
	while(unit.sigGen == SIGGEN_FUNCTGEN && ch != 'F' && (ch < '0' || ch > '3') || unit.sigGen == SIGGEN_AWG && ch != 'A' && ch != 'F' && (ch < '0' || ch > '8')  );
	


	if(ch == 'F')				// If we're going to turn off siggen
	{
		printf("Signal generator Off\n");
		waveform = 8;		// DC Voltage
		pkpk = 0;				// 0V
		waveformSize = 0;
	}
	else
	if (ch == 'A' && unit.sigGen == SIGGEN_AWG)		// Set the AWG
	{
		arbitraryWaveform = (short*)malloc( unit.AWGFileSize * sizeof(short));
		memset(arbitraryWaveform, 0, unit.AWGFileSize * sizeof(short));

		waveformSize = 0;

		printf("Select a waveform file to load: ");
		scanf_s("%s", fileName, 128);
		if (fopen_s(&fp, fileName, "r") == 0) 
		{ // Having opened file, read in data - one number per line (max 8192 lines for PS3x04B & PS3x05B devices, 16384 for PS3x06B), with values in (0..4095)
			while (EOF != fscanf_s(fp, "%hi", (arbitraryWaveform + waveformSize))&& waveformSize++ < unit.AWGFileSize - 1);
			fclose(fp);
			printf("File successfully loaded\n");
		} 
		else 
		{
			printf("Invalid filename\n");
			return;
		}
	}
	else			// Set one of the built in waveforms
	{
		switch (choice)
		{
			case 0:
				waveform = PS3000A_SINE;
				break;

			case 1:
				waveform = PS3000A_SQUARE;
				break;

			case 2:
				waveform = PS3000A_TRIANGLE;
				break;

			case 3:
				waveform = PS3000A_DC_VOLTAGE;
				do 
				{
					printf("\nEnter offset in uV: (0 to 2500000)\n"); // Ask user to enter DC offset level;
					scanf_s("%lu", &offset);
				} while (offset < 0 || offset > 10000000);
				break;

			case 4:
				waveform = PS3000A_RAMP_UP;
				break;

			case 5:
				waveform = PS3000A_RAMP_DOWN;
				break;

			case 6:
				waveform = PS3000A_SINC;
				break;

			case 7:
				waveform = PS3000A_GAUSSIAN;
				break;

			case 8:
				waveform = PS3000A_HALF_SINE;
				break;

			default:
				waveform = PS3000A_SINE;
				break;
		}
	}

	if(waveform < 8 || (ch == 'A' && unit.sigGen == SIGGEN_AWG))				// Find out frequency if required
	{
		do 
		{
			printf("\nEnter frequency in Hz: (1 to 1000000)\n"); // Ask user to enter signal frequency;
			scanf_s("%lu", &frequency);
		} while (frequency <= 0 || frequency > 1000000);
	}

	if (waveformSize > 0)		
	{
		delta = ((1.0 * frequency * waveformSize) / unit.AWGFileSize) * (4294967296.0 * 5e-8); // delta >= 10
	
		status = ps3000aSetSigGenArbitrary(	unit.handle, 
																				0,												// offset voltage
																				pkpk,									// PkToPk in microvolts. Max = 4uV  +2v to -2V
																				(unsigned long)delta,			// start delta
																				(unsigned long)delta,			// stop delta
																				0, 
																				0, 
																				arbitraryWaveform, 
																				waveformSize, 
																				(PS3000A_SWEEP_TYPE)0,
																				(PS3000A_EXTRA_OPERATIONS)0, 
																				PS3000A_SINGLE, 
																				0, 
																				0, 
																				PS3000A_SIGGEN_RISING,
																				PS3000A_SIGGEN_NONE, 
																				0);

		printf(status?"\nps3000aSetSigGenArbitrary: Status Error 0x%x \n":"", (unsigned int)status);		// If status != 0, show the error
	} 
	else 
	{
		status = ps3000aSetSigGenBuiltIn(unit.handle, 
																		offset, 
																		pkpk, 
																		waveform, 
																		(float)frequency, 
																		(float)frequency, 
																		0, 
																		0, 
																		(PS3000A_SWEEP_TYPE)0, 
																		(PS3000A_EXTRA_OPERATIONS)0, 
																		0, 
																		0, 
																		(PS3000A_SIGGEN_TRIG_TYPE)0, 
																		(PS3000A_SIGGEN_TRIG_SOURCE)0, 
																		0);
		printf(status?"\nps3000aSetSigGenBuiltIn: Status Error 0x%x \n":"", (unsigned int)status);		// If status != 0, show the error
	}
}


/****************************************************************************
* CollectStreamingImmediate
*  this function demonstrates how to collect a stream of data
*  from the unit (start collecting immediately)
***************************************************************************/
void CollectStreamingImmediate(UNIT * unit)
{
	struct tPwq pulseWidth;
	struct tTriggerDirections directions;

	memset(&pulseWidth, 0, sizeof(struct tPwq));
	memset(&directions, 0, sizeof(struct tTriggerDirections));

	SetDefaults(unit);

	printf("Collect streaming...\n");
	printf("Data is written to disk file (stream.txt)\n");
	printf("Press a key to start\n");
	_getch();

	/* Trigger disabled	*/
	SetTrigger(unit->handle, NULL, 0, NULL, 0, &directions, &pulseWidth, 0, 0, 0);

	StreamDataHandler(unit, 0);
}

/****************************************************************************
* CollectStreamingTriggered
*  this function demonstrates how to collect a stream of data
*  from the unit (start collecting on trigger)
***************************************************************************/
void CollectStreamingTriggered(UNIT * unit)
{
	short triggerVoltage = mv_to_adc(1000,	unit->channelSettings[PS3000A_CHANNEL_A].range, unit); // ChannelInfo stores ADC counts
	struct tPwq pulseWidth;

	struct tPS3000ATriggerChannelProperties sourceDetails = {	triggerVoltage,
																256 * 10,
																triggerVoltage,
																256 * 10,
																PS3000A_CHANNEL_A,
																PS3000A_LEVEL };

	struct tPS3000ATriggerConditions conditions = {	PS3000A_CONDITION_TRUE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE,
													PS3000A_CONDITION_DONT_CARE };

	struct tTriggerDirections directions = {	PS3000A_RISING,
												PS3000A_NONE,
												PS3000A_NONE,
												PS3000A_NONE,
												PS3000A_NONE,
												PS3000A_NONE };

	memset(&pulseWidth, 0, sizeof(struct tPwq));

	printf("Collect streaming triggered...\n");
	printf("Data is written to disk file (stream.txt)\n");
	printf("Press a key to start\n");
	_getch();
	SetDefaults(unit);

	/* Trigger enabled
	* Rising edge
	* Threshold = 1000mV */
	SetTrigger(unit->handle, &sourceDetails, 1, &conditions, 1, &directions, &pulseWidth, 0, 0, 0);

	StreamDataHandler(unit, 100000);
}


/****************************************************************************
* DisplaySettings 
* Displays information about the user configurable settings in this example
* Parameters 
* - unit        pointer to the UNIT structure
*
* Returns       none
***************************************************************************/
void DisplaySettings(UNIT *unit)
{
	int ch;
	int voltage;

	printf("\n\nReadings will be scaled in (%s)\n", (scaleVoltages)? ("mV") : ("ADC counts"));

	for (ch = 0; ch < unit->channelCount; ch++)
	{
		voltage = inputRanges[unit->channelSettings[ch].range];
		printf("Channel %c Voltage Range = ", 'A' + ch);

		if (voltage == 0)
			printf("Off\n");
		else
		{
			if (voltage < 1000)
				printf("%dmV\n", voltage);
			else
				printf("%dV\n", voltage / 1000);
		}
	}
	printf("\n");
}

/****************************************************************************
* OpenDevice 
* Parameters 
* - unit        pointer to the UNIT structure, where the handle will be stored
*
* Returns
* - PICO_STATUS to indicate success, or if an error occurred
***************************************************************************/
PICO_STATUS OpenDevice(UNIT *unit)
{
	char ch;
	short value = 0;
	int i;
	struct tPwq pulseWidth;
	struct tTriggerDirections directions;
	PICO_STATUS status = ps3000aOpenUnit(&(unit->handle), NULL);

	if (status == PICO_POWER_SUPPLY_NOT_CONNECTED)
		status = ChangePowerSource(unit->handle, status);

	printf("Handle: %d\n", unit->handle);
	if (status != PICO_OK) 
	{
		printf("Unable to open device\n");
		printf("Error code : 0x%08lx\n", status);
		while(!_kbhit());
		exit(99); // exit program
	}

	printf("Device opened successfully, cycle %d\n\n", ++cycles);

	// setup devices
	get_info(unit);
	timebase = 1;

	ps3000aMaximumValue(unit->handle, &value);
	unit->maxValue = value;

	for ( i = 0; i < PS3000A_MAX_CHANNELS; i++) 
	{
		unit->channelSettings[i].enabled = TRUE;
		unit->channelSettings[i].DCcoupled = TRUE;
		unit->channelSettings[i].range = PS3000A_5V;
	}

	memset(&directions, 0, sizeof(struct tTriggerDirections));
	memset(&pulseWidth, 0, sizeof(struct tPwq));

	SetDefaults(unit);

	/* Trigger disabled	*/
	SetTrigger(unit->handle, NULL, 0, NULL, 0, &directions, &pulseWidth, 0, 0, 0);

	return status;
}



/****************************************************************************
* CloseDevice 
****************************************************************************/
void CloseDevice(UNIT *unit)
{
	ps3000aCloseUnit(unit->handle);
}

/****************************************************************************
* main
* 
***************************************************************************/
int main(void)
{
	char ch;
	PICO_STATUS status;
	UNIT unit;

	printf("PS3000A driver example program\n");
	printf("\n\nOpening the device...\n");

	status = OpenDevice(&unit);


	ch = '.';
	while (ch != 'X')
	{
		DisplaySettings(&unit);

		printf("\n\n");
		printf("B - Immediate block                           V - Set voltages\n");
		printf("T - Triggered block                           I - Set timebase\n");
		printf("E - Collect a block of data using ETS         A - ADC counts/mV\n");
		printf("R - Collect set of rapid captures\n");
		printf("S - Immediate streaming\n");
		printf("W - Triggered streaming\n");
		printf(unit.sigGen != SIGGEN_NONE?"G - Signal generator\n":"");
		printf("                                              X - Exit\n");
		printf("Operation:");

		ch = toupper(_getch());

		printf("\n\n");
		switch (ch) 
		{
		case 'B':
			CollectBlockImmediate(&unit);
			break;

		case 'T':
			CollectBlockTriggered(&unit);
			break;

		case 'R':
			CollectRapidBlock(&unit);
			break;

		case 'S':
			CollectStreamingImmediate(&unit);
			break;

		case 'W':
			CollectStreamingTriggered(&unit);
			break;

		case 'E':
			if(unit.ETS == FALSE)
			{
				printf("This model does not support ETS\n\n");
				break;
			}

			CollectBlockEts(&unit);
			break;

		case 'G':
			if(unit.sigGen == SIGGEN_NONE)
			{
				printf("This model does not have a signal generator\n\n");
				break;
			}

			SetSignalGenerator(unit);
			break;

		case 'V':
			SetVoltages(&unit);
			break;

		case 'I':
			SetTimebase(unit);
			break;

		case 'A':
			scaleVoltages = !scaleVoltages;
			break;

		case 'X':
			break;

		default:
			printf("Invalid operation\n");
			break;
		}
	}
	CloseDevice(&unit);

	return 1;
}

#ifndef  __PS3000ACONN_HHH___
#define  __PS3000ACONN_HHH___


#include <stdio.h>

/* Headers for Windows */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN 
#include "windows.h"
#include <conio.h>
#include "ps3000aApi.h"
#else
#include <sys/types.h>
#include <string.h>

#include <libps3000a-1.0/ps3000aApi.h>
#include "linux_utils.h"
#endif


#define QUAD_SCOPE		4
#define DUAL_SCOPE		2

// AWG Parameters

#define AWG_DAC_FREQUENCY			20e6		
#define AWG_DAC_FREQUENCY_PS3207B	100e6
#define	AWG_PHASE_ACCUMULATOR		4294967296.0

typedef enum
{
	ANALOGUE,
	DIGITAL,
	AGGREGATED,
	MIXED
}MODE;


typedef struct
{
	int16_t DCcoupled;
	int16_t range;
	int16_t enabled;
}CHANNEL_SETTINGS;

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
	PS3000A_PWQ_CONDITIONS_V2 * conditions;
	int16_t nConditions;
	PS3000A_THRESHOLD_DIRECTION direction;
	uint32_t lower;
	uint32_t upper;
	PS3000A_PULSE_WIDTH_TYPE type;
}PWQ;

typedef struct
{
	int16_t					handle;
	int8_t					model[8];
	PS3000A_RANGE			firstRange ;
	PS3000A_RANGE			lastRange;
	int16_t					channelCount;
	int16_t					maxValue;
	int16_t					sigGen;
	int16_t					ETS;
	int32_t					AWGFileSize;
	CHANNEL_SETTINGS		channelSettings [PS3000A_MAX_CHANNELS];
	int16_t					digitalPorts;
}UNIT;

uint32_t	timebase = 8;
int16_t     oversample = 1;
BOOL		scaleVoltages = TRUE;

uint16_t inputRanges [PS3000A_MAX_RANGES] = {10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000};

BOOL     	g_ready = FALSE;
int32_t 	g_times [PS3000A_MAX_CHANNELS] = {0, 0, 0, 0};
int16_t     g_timeUnit;
int32_t     g_sampleCount;
uint32_t	g_startIndex;
int16_t		g_autoStopped;
int16_t		g_trig = 0;
uint32_t	g_trigAt = 0;

char BlockFile[20]		= "block.txt";
char DigiBlockFile[20]	= "digiBlock.txt";
char StreamFile[20]		= "stream.txt";

typedef struct tBufferInfo
{
	UNIT * unit;
	MODE mode;
	int16_t **driverBuffers;
	int16_t **appBuffers;
	int16_t **driverDigBuffers;
	int16_t **appDigBuffers;

} BUFFER_INFO;

int32_t timeInterval;
unsigned long _timeout=500; // 5s




//extern  void   picoInitBlock(UNIT * unit,long sampleOffset_);
//extern void picoInitRapidBlock(UNIT * unit,long sampleOffset_,unsigned long timeout);
//extern void closeDevice(UNIT *unit);
//extern void picoSetVoltages(UNIT * unit,short rangeSet);
//extern void picoSetTimebase(UNIT *unit,unsigned long timebase_);
//extern PICO_STATUS openDevice(UNIT *unit);
//extern PICO_STATUS picoRunRapidBlock(UNIT * unit,unsigned short nCaptures,unsigned long nSamples,unsigned long *CompletedNSample,unsigned long *nCompletedCaptures,short * pBuf);


#endif

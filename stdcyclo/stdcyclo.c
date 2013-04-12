#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <stdcyclo.h>
#include <stdio.h>
#include <math.h>
#include <dlfcn.h>

typedef struct parm_
{
	char name[60];
	float value;
} parm_t;
parm_t * parms = NULL;
int parmCount = 0;
unsigned int prmLoadTime;

void uplink()
{
	void * glb;
	glb = dlopen(NULL, RTLD_WORLD|RTLD_GROUP);
	raiseError_ = dlsym(glb,"raiseError");
	initAction_ = dlsym(glb,"initAction");
	addTag_ = dlsym(glb,"addTag");
	getTag_ = dlsym(glb,"getTag");
	sendTag_ = dlsym(glb,"sendTag");
	dropFunc_ = dlsym(glb,"dropFunc");
	bustFunc_ = dlsym(glb,"bustFunc");
	return;
}

void hysteresis(void * argRef, 
				char* parmName,
				int parmSource, 
				char* relayName,
				int relaySource, 
				float low, 
				float high, 
				char heater)
{
	relayArgs_t arg;
	char on_low = 0;
	float value;
	arg = *((relayArgs_t*)argRef);
	if(heater) on_low = 1;
	if(arg.firstRun)
	{
		addTag_(parmName,parmSource,TYPE_REAL,arg.n,SUBSCRIBE);
		addTag_(relayName,relaySource,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
	}
	else
	{
		value = getTag_(parmName).value.flt;
		if(value < low)  sendTag_(relayName, on_low, STATUS_OK);
		if(value > high) sendTag_(relayName, 1 - on_low, STATUS_OK);
	}
	dropFunc_(argRef);
	return;
}

void hysteresis_w_enable(void * argRef, 
						 char* parmName,
						 int parmSource, 
						 char* relayName, 
						 int relaySource, 
						 char * enable, 
						 int enableSource, 
						 float low, 
						 float high, 
						 char heater)
{
	relayArgs_t arg;
	char on_low = 0;
	static int enabled = 3;
	int enable_val;
	float value;
	arg = *((relayArgs_t*)argRef);
	if(heater) on_low = 1;
	if(arg.firstRun)
	{
		addTag_(parmName,parmSource,TYPE_REAL,arg.n,SUBSCRIBE);
		addTag_(enable,enableSource,TYPE_REAL,arg.n,SUBSCRIBE);
		addTag_(relayName,relaySource,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
	}
	else
	{
		enable_val = getTag_(enable).value.flt;
		enabled = getTag_(relayName).value.dsc;
				value = getTag_(parmName).value.flt;
		switch(enable_val)
		{
			case 0:
				if(enabled !=0) sendTag_(relayName, 0, STATUS_OK);
				break;
			case 1:
				if(value < low & enabled != on_low)  sendTag_(relayName, on_low, STATUS_OK);
				if(value > high & enabled == on_low) sendTag_(relayName, 1 - on_low, STATUS_OK);
				break;
			case 2:
				if(enabled !=1) sendTag_(relayName, 1, STATUS_OK);
		}
	}
	dropFunc_(argRef);
	return;
}

void linear(void * argRef, 
			char* parmName,
			int parmSource, 
			char* openName,
			int openSource, 
			char* closeName,
			int closeSource, 
			char* ruName, 
			int ruSource, 
			char * enable, 
			int enableSource, 
			float min, 
			float max,
			float ruMax,
			float hyst)
{
	relayArgs_t arg;
	float ruPos;
	float level;
	int enabled;
	int opened;
	int closed;
	arg = *((relayArgs_t*)argRef);
	if(arg.firstRun)
	{
		addTag_(parmName,parmSource,TYPE_REAL,arg.n, SUBSCRIBE);
		addTag_(enable,enableSource,TYPE_DIGITAL,arg.n, SUBSCRIBE);
		addTag_(ruName,ruSource,TYPE_REAL,arg.n, SUBSCRIBE);
		addTag_(openName,openSource,TYPE_DIGITAL,arg.n, NOT_SUBSCRIBE);
		addTag_(closeName,closeSource,TYPE_DIGITAL,arg.n, NOT_SUBSCRIBE);
		
	}
	else
	{
		enabled = getTag_(enable).value.dsc;
		opened = getTag_(openName).value.dsc;
		closed = getTag_(closeName).value.dsc;
		level = getTag_(parmName).value.flt;
		ruPos = getTag_(ruName).value.flt;
		if(enabled)
		{
			if(level > max) level = max;
			ruPos = ruPos/ruMax;
			level = (level-min)/(max-min);
			if(level < 0) level = 0;
			if(level < ruPos)
			{
				if(opened) sendTag_(openName, 0, STATUS_OK);
				if(level < ruPos - hyst)
				{
					if(!closed) sendTag_(closeName, 1, STATUS_OK);
				}
			}
			else
			{
				if(closed) sendTag_(closeName, 0, STATUS_OK);
				if(level > ruPos + hyst)
				{
					if(!opened) sendTag_(openName, 1, STATUS_OK);
				}
			}
		}
	}
	dropFunc_(argRef);
	return;
}

void delay_ms(long ms)
{
	struct timespec t_delay;
	if(ms < 10)
		return;
	t_delay.tv_sec = (int)(ms/1000);
	t_delay.tv_nsec = (ms/1000.0 - floor(ms/1000.0))*1E9;
	nanosleep(&t_delay, NULL);
	return;
}

int parmCmp(const void *pt1, const void *pt2)
{
	parm_t p1, p2;
	p1 = * (parm_t*)pt1;
	p2 = * (parm_t*)pt2;
	return strncmp(p1.name,p2.name, 60);
}
void ldParms(char* file)
{
	FILE * parmFile;
	struct stat prmState;
	stat(file, &prmState);
	prmLoadTime = prmState.st_mtime; 
	parmFile = fopen(file, "r");
	fread(&parmCount, sizeof(int), 1, parmFile);
	parms = realloc(parms, sizeof(parm_t)*parmCount);
	fread(parms, sizeof(parm_t), parmCount, parmFile);
	qsort(parms, parmCount,sizeof(parm_t), parmCmp);
	fclose(parmFile);
}

float getParm(char* name)
{
	parm_t * ret_ref = NULL;
	parm_t ret; 
	struct stat prmState;
	strncpy(ret.name, name, 60);
	ret.value = 0;
	stat("parms.lst", &prmState);
	if(prmState.st_mtime != prmLoadTime) ldParms("parms.lst");
	if(parmCount == 0) ldParms("parms.lst");
	if(parmCount > 0 && parms != NULL)
	{
		ret_ref = bsearch(&ret, parms, parmCount, sizeof(parm_t), parmCmp);
	}
	if(ret_ref != NULL) ret = *ret_ref;
	return ret.value;
}

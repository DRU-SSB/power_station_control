#include <../relay/relay_defs.h>

#ifndef STDCYCLO_H_
#define STDCYCLO_H_

#define HEATER 1
#define FRIGE 0


/*void uplink(void raiseError_(unsigned int, char*, char),
				void initAction_(void* (*function)(void*), int),
				char addTag_(char*, char, char, int, char),
				tag_t getTag_(char*),
				void sendTag_(char*, int, char),
				void dropFunc_(void *),
				void bustFunc_(void *));
*/
void hysteresis(void *, 
				char*,
				int, 
				char*,
				int, 
				float, 
				float, 
				char);
void hysteresis_w_enable(void *, 
						 char*,
						 int, 
						 char*, 
						 int, 
						 char *, 
						 int, 
						 float, 
						 float, 
						 char);
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
			float hyst);

void delay_ms(long);
float getParm(char* name);

void (*raiseError_)(unsigned int, char*, char);
void (*initAction_)(int, char*, int);
char (*addTag_)(char*, char, char, int, char);
tag_t (*getTag_)(char*);
void (*sendTag_)(char*, int, char);
void (*dropFunc_)(void *);
void (*bustFunc_)(void *);
#endif


#include <pthread.h>

#ifndef RELAY_DEFS_H_
#define RELAY_DEFS_H_

#define MUTEX_ERROR 1
#define MEMORY_FAULT 2
#define SEGFAULT_PREV 3
#define TAG_RG_FAIL 4
#define GSRD_FAIL 5
#define TAG_TYPE_WRONG 6
#define TAG_READ_BEFORE_RG 7
#define LIB_CONNECTION_FAILED 8
#define UNRESOLVED_SYMBOL 9

#define STATUS_OK 0
#define STATUS_FAIL 2

#define TYPE_REAL 2
#define TYPE_DIGITAL 1

#define TAGNAMESZ 60

#define QUERY_DELAY 100

#define RUN_MODAL 0
#define RUN_THREAD 1
#define RUN_MULTIPLE 2
#define RUNNING 4

#define CRITICAL 1
#define NONCRITICAL 0

#define SUBSCRIBE 0
#define NOT_SUBSCRIBE 1


typedef struct _th_args
{
	int tagCount;
	int delay;
	int source;
} thArgs_t;
typedef struct _relayArgs
{
	int n;
	int firstRun;
} relayArgs_t;

typedef union _val_union
{ 
		float flt;
		int dsc;
} val_union;

typedef struct _actionID     // действие по событию
{
	int n;				//номер функции в списке
	void* next;			//ссылка на следующую струкутру callback
} actionID_t;

typedef struct _tag
{
	char name[TAGNAMESZ];
	char state;					//статус тэга
	char source;
	char type;				//идентификатор контроллера
	val_union value;
	actionID_t * subscription;	//ссылка на первое действие
} tag_t;

typedef struct _src
{
	char deviceName[60];
	int devFD;
	pthread_t tid;
	pthread_mutex_t mutex; 
} src_t;

///////////////// объ€влени€ дл€ списка действий
typedef struct _action     // подробности действи€
{
	void* (*entryPoint)(void*);				//ссылка на функцию
	int callType;							//параметры вызова
	pthread_t th_id;
} action_t;

#endif /*RELAY_DEFS_H_*/

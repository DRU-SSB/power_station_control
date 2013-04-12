#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <relay_defs.h>
#include <dlfcn.h>

///////////////// объявления для списка тэгов
pthread_rwlock_t tagListRWLock;		     // запрещаем обращаться к таблице 
                             			// тэгов во время обновления


action_t * actions;					//массив действий
unsigned int actionCount = 0;		//число действий
tag_t* usedTags = NULL;  			//массив используемых тэгов
unsigned int tagCount = 0;		// число тэгов в usedTags

src_t sources[3];
typedef struct _libnode
{
	void * lib;
/*	void (*uplink)(void raiseError_(unsigned int, char*, char),
				void initAction_(void* (*function)(void*), int),
				char addTag_(char*, char, char, int, char),
				tag_t getTag_(char*),
				void sendTag_(char*, int, char),
				void dropFunc_(void *), void bustFunc_(void *));
*/
	void (*uplink)(void);
	void (*init_lib)(int libno);
} libnode_t;
libnode_t libs[2];	
/////////////// структуры для общения с GSRD
typedef struct _pckHeader
{
	unsigned short cmd;
	unsigned short direction;
	unsigned int parm1;
	unsigned int parm2;
} pckHeader_t;

typedef struct _queryTag
{
    char name[TAGNAMESZ];
    unsigned long type;
    unsigned long status;
    unsigned long flg;
    unsigned long lnk1;
    unsigned long lnk2;
    unsigned long lnk3;
} queryTag_t;
typedef struct _replyTag
{
    char name[TAGNAMESZ];
    unsigned int type;
    unsigned int status;
    unsigned int val1;
    unsigned int val2;
    unsigned int unixtime;
    unsigned int dtime;
} replyTag_t;



///////////////// Обработчик ошибок
void raiseError(unsigned int n, char* msg, char critical)
{
	FILE * errlog;
	time_t seconds;
	seconds = time(NULL);
	errlog = fopen("relayErr.log", "a");
	fprintf(errlog, "%s::(%d)%s\n", ctime(&seconds),n,msg);
	fclose(errlog);
	if(critical == CRITICAL)
	{
		abort();
	}
}


///////////////// Сравнение имен тэгов для поиска и сортировки
int tagcmp(const void *p1, const void *p2)
{
	tag_t t1, t2;
	t1 = * (tag_t*)p1;
	t2 = * (tag_t*)p2;
	return strncmp(t1.name,t2.name, TAGNAMESZ);
}
///////////////// Выполнение действий 
char avalanchExec(tag_t execTag)
{
	actionID_t * action = NULL;
	pthread_attr_t threadAttr;
	struct sched_param parm;
	relayArgs_t * argument;
	pthread_t thread;
	int i = 0;
/*	//////////////////////////////////////////// Этот код выводит на экран какие функции на какие тэги подписаны
	int j = 0;
	actionID_t * s = NULL;
	for (j = 0; j < tagCount; j++)
	{
		printf("!TAG: %s ", usedTags[j].name);
		s = usedTags[j].subscription;
		while(s != NULL)
		{
			printf("%d ", s->n);
			s = s->next;
		}
		printf("\n");
	}	
*/	////////////////////////////////////////////
	action = execTag.subscription;
	while(action != NULL)
	{
		i++; 
		argument = malloc(sizeof(relayArgs_t));
		argument->n = action->n;
		argument->firstRun = 0;
//		printf("TAG: %s, N = %d\n", execTag.name, action->n);
		if((actions[action->n].callType & RUN_THREAD) == 0)
		{
			actions[action->n].entryPoint(argument);
		}
		else
		{
			pthread_attr_init(&threadAttr);
			pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_DETACHED);
			parm.sched_priority = 50;
			pthread_attr_setinheritsched(&threadAttr, PTHREAD_EXPLICIT_SCHED);
			pthread_attr_setschedpolicy(&threadAttr, SCHED_RR);
			pthread_attr_setschedparam(&threadAttr, &parm);
			if(actions[action->n].callType & RUN_MULTIPLE)
			{ 
				pthread_create(NULL,&threadAttr, actions[action->n].entryPoint,argument);
				actions[action->n].th_id = 0;
			}
			else
			{
				if((actions[action->n].callType & RUNNING) == 0)
				{
					actions[action->n].callType|=RUNNING;
					pthread_create(&thread,&threadAttr, actions[action->n].entryPoint,argument);
					actions[action->n].th_id = thread;
				}
				else
				{
					free(argument);
				}
			}
		}
		action = action->next;
	}
	return 0;
}
///////////////// Добавление действия
char addAction(void* (*function)(void*), int params) 
{
	if(actionCount == 0 || actions == NULL)
	{
		actions = malloc(sizeof(action_t));
		if(actions == NULL)
		{
			raiseError(MEMORY_FAULT, "addAction", CRITICAL);
			return 1;
		}
		actions[0].callType = params;
		actions[0].entryPoint = function;
		actionCount = 1;
	}
	else //от if(actionCount == 0 || actions == NULL)
	{
		actionCount++;
		actions = realloc(actions, actionCount*sizeof(action_t));
		if(actions == NULL)
		{
			raiseError(MEMORY_FAULT, "addAction", CRITICAL);
			return -1;
		}
		actions[actionCount -1].callType = params;
		actions[actionCount -1].entryPoint = function;
		
	}
	
	return actionCount - 1;
}
//подписка существующего действия на существующий тэг
char subscribeAction(tag_t* tagRef, unsigned int n) //NOT THREAD SAFE!
{
	char notFound = 1;
	actionID_t* now = NULL;
	actionID_t* prev = NULL;
	actionID_t* new = NULL;
	if(n >= actionCount)
	{
		raiseError(3, "Program Error. Segfault prevented.", NONCRITICAL);
		return 1;
	}
	now = tagRef->subscription;
	while(notFound && (now != NULL))
	{
		prev = now;
		now = (actionID_t*)(now->next);
		if(prev->n ==n)
			notFound = 0;
	}
	if(notFound)
	{
		new = malloc(sizeof(actionID_t));
		if(new == NULL)
		{
			raiseError(MEMORY_FAULT, "subscribeAction", NONCRITICAL);
			return 1;
		}
		new->n = n;
		new->next = NULL;
		if(prev == NULL)
			tagRef->subscription = new;
		else
		{
			prev->next = new;
		}
	}
	else // от if(notFound)
		return 2;
	return 0;
}

///////////////// Добавление тэга
char addTag(char* tagName, char source, char type, int actionNum, char subscribe)
{
	char sortRQ = 0;
	tag_t * tag_to_subscribe;
	tag_t new;

	if(pthread_rwlock_wrlock(&tagListRWLock))
	{
		raiseError(MUTEX_ERROR, "addTag", CRITICAL);
		return 1;
	}
	if(tagCount == 0 || usedTags == NULL)
	{
		usedTags = malloc(sizeof(tag_t));
		if(usedTags == NULL)
		{
			raiseError(MEMORY_FAULT, "addTag", CRITICAL);
			return 1;
		}
		usedTags[0].source = source;
		usedTags[0].subscription = NULL;
		usedTags[0].state = 255;
		usedTags[0].type = type;
		
		strncpy(usedTags[0].name, tagName, TAGNAMESZ);
		tagCount = 1;
		tag_to_subscribe = usedTags;
	}
	else //от if(tagCount == 0 || usedTags == NULL)
	{
		
		strncpy(new.name, tagName, TAGNAMESZ);
		tag_to_subscribe = bsearch(&new, usedTags, tagCount, sizeof(tag_t), tagcmp); 
		if(tag_to_subscribe == NULL)
		{
			tagCount++;
			usedTags = realloc(usedTags, tagCount*sizeof(tag_t));
			if(usedTags == NULL)
			{
				raiseError(MEMORY_FAULT, "addTag", CRITICAL);
				return 1;
			}
			usedTags[tagCount -1].source = source;
			usedTags[tagCount -1].subscription = NULL;
			usedTags[tagCount -1].state = 255;
			usedTags[tagCount -1].type = type;
			strncpy(usedTags[tagCount -1].name, tagName, TAGNAMESZ);
			sortRQ = 1;
			tag_to_subscribe = &(usedTags[tagCount -1]);
		}
	}
	if(subscribe == SUBSCRIBE)
	{
		subscribeAction(tag_to_subscribe, actionNum);
	}
	if(sortRQ)
		qsort(usedTags, tagCount,sizeof(tag_t), tagcmp);
		
	if(pthread_rwlock_unlock(&tagListRWLock))
	{
		raiseError(MUTEX_ERROR, "addTag", CRITICAL);
		return 1;
	}
	return 0;
}



tag_t getTag(char* name)
{
	tag_t * ret_ref = NULL;
	tag_t ret;
	char d[67];
	strncpy(ret.name, name, TAGNAMESZ);
	ret.state = 127;
	if(pthread_rwlock_rdlock(&tagListRWLock))
	{
		raiseError(MUTEX_ERROR, "getTag", CRITICAL);
		return ret;
	}
	if(tagCount > 0 & usedTags !=NULL)
	{
		ret_ref = bsearch(&ret, usedTags, tagCount, sizeof(tag_t), tagcmp);
	}
	if(ret_ref != NULL)
	{
		ret = *ret_ref;
	}
	else
	{
		sprintf(d, "getTag %s", name);
		raiseError(TAG_READ_BEFORE_RG, d, NONCRITICAL);
	}
	if(pthread_rwlock_unlock(&tagListRWLock))
	{
		raiseError(MUTEX_ERROR, "getTag", CRITICAL);
		return ret;
	}

	return ret;
}

void sendTag(char* name, int value, char state)
{
	tag_t * ret_ref = NULL;
	tag_t ret;
	int source;
	pckHeader_t * head;
	replyTag_t * sTag;
	void* buffer;
	
	strncpy(ret.name, name, TAGNAMESZ);
	if(pthread_rwlock_wrlock(&tagListRWLock))
	{
		raiseError(MUTEX_ERROR, "sendTag", CRITICAL);
		return;
	}
	if(tagCount > 0 && usedTags !=NULL)
	{
		ret_ref = bsearch(&ret, usedTags, tagCount, sizeof(tag_t), tagcmp);
	}
	if(ret_ref != NULL)
	{
		
		ret = *ret_ref;
		if(ret_ref->type == TYPE_DIGITAL)
		{
			ret_ref->value.dsc = value;
		}
		if(pthread_rwlock_unlock(&tagListRWLock))
		{
			raiseError(MUTEX_ERROR, "sendTag", CRITICAL);
			return;
		}
		source = ret.source;
		if(sources[source].devFD == 0)
		{
			raiseError(GSRD_FAIL, "sendTag", NONCRITICAL);
		}
		else
		{
			buffer = malloc(sizeof(pckHeader_t) + sizeof(replyTag_t));
			head = buffer;
			sTag = buffer + sizeof(pckHeader_t);
			head->cmd = 2;
			head->direction = 0;
			head->parm1 = 0;
			head->parm2 = 1;
			strncpy(sTag->name, name, TAGNAMESZ);
			sTag->type = ret.type;
			sTag->val1 = value;
			sTag->status = state;
			sTag->val2 = 0;
			sTag->unixtime = 0;
			sTag->dtime = 0;
			pthread_mutex_lock(&(sources[source].mutex));
			write(sources[source].devFD,(void*)head, sizeof(pckHeader_t) + sizeof(replyTag_t));
			pthread_mutex_unlock(&(sources[source].mutex));
			free(buffer);	
		}
	}
	else
	{
		if(pthread_rwlock_unlock(&tagListRWLock))
		{
			raiseError(MUTEX_ERROR, "sendTag", CRITICAL);
			return;
		}
	}	
	return;
}
	
int setTag(char* name, int value, char state)
{
	tag_t * ret_ref = NULL;
	tag_t ret;
	
	strncpy(ret.name, name, TAGNAMESZ);
	if(pthread_rwlock_wrlock(&tagListRWLock))
	{
		raiseError(MUTEX_ERROR, "setTag", CRITICAL);
		return -1;
	}
	if(tagCount > 0 && usedTags !=NULL)
	{
		ret_ref = bsearch(&ret, usedTags, tagCount, sizeof(tag_t), tagcmp);
	}
	if(ret_ref != NULL)
	{
		
		ret_ref->state = state;
		ret_ref->value.dsc = value;
		ret = *ret_ref;
		if(pthread_rwlock_unlock(&tagListRWLock))
		{
			raiseError(MUTEX_ERROR, "setTag", CRITICAL);
			return -1;
		}
		avalanchExec(ret);
	}
	else
	{
		if(pthread_rwlock_unlock(&tagListRWLock))
		{
			raiseError(MUTEX_ERROR, "setTag", CRITICAL);
			return -1;
		}
		return -1;
	}
	return 0;
}


int connect(int source)
{
	int i;
	int real_tags = 0;
	queryTag_t * qtag;
	void * query;
	pckHeader_t * head; 
	pthread_mutex_lock(&(sources[source].mutex));
	if(sources[source].devFD != 0)
	{
		close(sources[source].devFD);
	}
	query = malloc(sizeof(pckHeader_t) + tagCount*sizeof(queryTag_t));
	memset(query, 0, sizeof(pckHeader_t) + tagCount*sizeof(queryTag_t));
	qtag = query + sizeof(pckHeader_t);
	head = query;
	head->cmd = 19;
	head->direction = 0;
	head->parm1 = 0;
	sources[source].devFD = open(sources[source].deviceName, O_RDWR);
	if(sources[source].devFD == 0)
	{
		raiseError(GSRD_FAIL, "Connect", NONCRITICAL);
		return -1;
	}
	for(i = 0; i<tagCount; i++)
	{
		if(usedTags[i].source == source)
		{
			qtag[real_tags].status = 0;
			qtag[real_tags].lnk1 = 0;
			qtag[real_tags].lnk2 = 0;
			qtag[real_tags].lnk3 = 0;
			strncpy(qtag[real_tags].name, usedTags[i].name, TAGNAMESZ);
			switch(usedTags[i].type)
			{
			case TYPE_DIGITAL:
				qtag[real_tags].type = 1;
				qtag[real_tags].flg = 1;
				real_tags++;
				break;
			case TYPE_REAL:
				qtag[real_tags].type = 2;
				qtag[real_tags].flg = 3;
				real_tags++;
				break;
			default:
				raiseError(TAG_TYPE_WRONG, "Connect", NONCRITICAL);
			}
		}
	}
	head->parm2 = real_tags;
	write(sources[source].devFD, query, sizeof(pckHeader_t) + real_tags*sizeof(queryTag_t));
	
	head->cmd = 21;
	head->direction = 0;
	head->parm1 = 0;
	head->parm2 = 0;
	write(sources[source].devFD, query, sizeof(pckHeader_t));
	read(sources[source].devFD, query, sizeof(pckHeader_t));
	if(head->parm1 != real_tags)
	{
		raiseError(TAG_RG_FAIL, "Connect", NONCRITICAL);
		real_tags = head->parm1;
	}
	free(query);
	pthread_mutex_unlock(&(sources[source].mutex));
	return real_tags;
}
////////////// Функция опроса тэгов. отдельный поток.
void * queryCycle(void * args)
{
	void * buffer;
	int bufLen;
	int i;
	int source;
	struct timespec t_delay;
	pckHeader_t qry;
	pckHeader_t *rpl;
	replyTag_t * recTag;
	int tagCount;
	int readBytes;
	tagCount = ((thArgs_t *)args)->tagCount;
	source = ((thArgs_t *)args)->source;
	bufLen = sizeof(pckHeader_t) + tagCount*sizeof(replyTag_t);
	buffer = malloc(bufLen);
	rpl = buffer;
	recTag = buffer + sizeof(pckHeader_t);
	qry.cmd = 20;
	qry.direction = 0;
	qry.parm1 = 0;
	qry.parm2 = 0;
	while(1)
	{
		if(tagCount >=0 && sources[source].devFD > 0)
		{
			pthread_mutex_lock(&(sources[source].mutex));
			write(sources[source].devFD, &qry, sizeof(pckHeader_t));
			readBytes = read(sources[source].devFD, buffer, bufLen);
			pthread_mutex_unlock(&(sources[source].mutex));
			if(readBytes < 3)
				tagCount = -1;
			for(i = 0; i <rpl->parm2; i++)
			{
					if(setTag(recTag[i].name, recTag[i].val1, recTag[i].status))
						tagCount = -1;
			}
		}
		else
		{
			free(buffer);
			tagCount = connect(source);
			if(tagCount >=0)
			{
				bufLen = sizeof(pckHeader_t) + tagCount*sizeof(replyTag_t);
				buffer = malloc(bufLen);
				rpl = buffer;
				recTag = buffer + sizeof(pckHeader_t);
			}
		}
		
	nsec2timespec(&t_delay, 250000000);
	nanosleep(&t_delay, NULL);
	//sleep(1);
	}
	return NULL;
}
///////////////// Запрос тэгов в контроллере

char openConn(int source)
{
	pthread_t tid;
	pthread_attr_t threadAttr;
	thArgs_t arg;
	int real_tags = 0;
	sources[source].devFD = 0;
	pthread_mutex_init(&(sources[source].mutex), NULL);
	real_tags = connect(source);
	arg.source = source;
	arg.tagCount = real_tags;
	arg.delay = QUERY_DELAY;
	pthread_attr_init(&threadAttr);
	pthread_create(&tid, &threadAttr, queryCycle, &arg);
	sources[source].tid = tid;
	return sources[source].devFD;
}


void dropFunc(void *arg)
{
	if(actions[((relayArgs_t*)arg)->n].callType & RUNNING)
	{
		actions[((relayArgs_t*)arg)->n].callType -= RUNNING;
		free(arg);
	}
	return;
}

void bustFunc(void *arg)
{
	if(actions[((relayArgs_t*)arg)->n].callType & RUNNING)
	{
		actions[((relayArgs_t*)arg)->n].callType -= RUNNING;
		pthread_cancel(actions[((relayArgs_t*)arg)->n].th_id);
	   	free(arg);
	}
	return;
}

void initAction(int libno, char *funcname, int params)
{
	relayArgs_t * a;
	void * (*function)(void*);
	a = malloc(sizeof(relayArgs_t));
	function = dlsym(libs[libno].lib,funcname);
	if(function != NULL)
	{
		a->n = addAction(function,params);
		a->firstRun = 1;
		function(a);
		printf("Init %s OK\n", funcname); 
	}
	else
	{
		raiseError(UNRESOLVED_SYMBOL, funcname, NONCRITICAL);
	}
	return;	
}
void con_lib (char * libname, int libno)
{
	
	libs[libno].lib = dlopen(libname, RTLD_LOCAL);
	if(libs[libno].lib == NULL)
	{
		libs[libno].init_lib = NULL;
		libs[libno].uplink = NULL;
		raiseError(LIB_CONNECTION_FAILED, libname, NONCRITICAL);
	}
	else
	{	
		libs[libno].init_lib = dlsym(libs[libno].lib, "init_lib");
		libs[libno].uplink = dlsym(libs[libno].lib, "uplink");
	}
	return;
}
void run_init(int libno)
{
	if(libs[libno].uplink != NULL)
	{
		libs[libno].uplink(); 
		libs[libno].init_lib(libno);
	}
	return;
}
int main(int argc, char *argv[]) {
	
	pthread_rwlockattr_t rwattr;
	pthread_rwlockattr_init(&rwattr);
	pthread_rwlock_init(&tagListRWLock, &rwattr);
	sources[0].devFD = 0;
	sources[1].devFD = 0;
	strncpy(sources[0].deviceName ,"/dev/ineum_gsrd", 60);
	strncpy(sources[1].deviceName ,"/net/QETK100KP2/dev/ineum_gsrd", 60);
	con_lib("libmixer.so", 0);
	con_lib("libreactor.so", 1);
	run_init(0);
	run_init(1);
	
	openConn(0);
	openConn(1);
	printf("Automation system started OK \n");
	pthread_join(sources[0].tid, NULL);
	return EXIT_SUCCESS;
}

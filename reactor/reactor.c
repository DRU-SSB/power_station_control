#include <stdlib.h>
#include <stdio.h>
#include <../relay/relay_defs.h>
#include <time.h>
#include <unistd.h>
#include <../stdcyclo/stdcyclo.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#define TEST
#define STOP_REASON_NORMAL 0
#define STOP_REASON_FILL_VALVE_JAMMED 1
#define STOP_REASON_DRAIN_VALVE_JAMMED 2
#define STOP_REASON_PNEUMATICS_LOW_PRESSURE 4
#define STOP_REASON_NVD_ERROR 8
#define STOP_REASON_BYPASS 16
#define STOP_REASON_NVD_HIGH_PRESURE 32

#define STATE_OFF 0
#define STATE_WAIT_MUTEX 1
#define STATE_WAIT_MIXER 2
#define STATE_FILL 3
#define STATE_BOIL 4
#define STATE_DRAIN 5
#define STATE_DWELL 6

#define DRAIN_TIME_CORRECTION "DRAIN_TIME_CORRECTION" 

#define NVD_RELAY "1_NVD_XC04"
//#define NVD_FB "1_NVD_XB03" 
#define NVD_FB "1_NVD_XC04"
#define NVD_EMERGENCY_OFF "1_NVD_XC01"
#define NVD_VALVE "1_EMK03_XC03"
#define NVD_BYPASS "1_PK01_XC03"
#define NVD_FLUSH "1_EMK02_XC03"

#define R1_VALVE_FILL "1_PK02_XC03"
#define R2_VALVE_FILL "1_PK09_XC03"
#define R1_VALVE_DRAIN "1_PK03_XC03"
#define R2_VALVE_DRAIN "1_PK08_XC03"

//#define R2_VALVE_DRAIN_FB_OPEN "1_PK08_XB01"

#ifndef TEST
#define R1_VALVE_FILL_FB_OPEN "1_PK02_XB01"
#define R2_VALVE_FILL_FB_OPEN "1_PK09_XB01"
#define R1_VALVE_DRAIN_FB_OPEN "1_PK03_XB01"
#define R2_VALVE_DRAIN_FB_OPEN "1_PK08_XB01"
#define NVD_BYPASS_FB_OPEN "1_PK01_XB01"
#else
#define R1_VALVE_FILL_FB_OPEN "1_PK02_XC03"
#define R2_VALVE_FILL_FB_OPEN "1_PK09_XC03"
#define R1_VALVE_DRAIN_FB_OPEN "1_PK03_XC03"
#define R2_VALVE_DRAIN_FB_OPEN "1_PK08_XC03"
#define NVD_BYPASS_FB_OPEN "1_PK01_XC03"
#endif

#define R1_VALVE_FILL_FB_CLOSE "1_PK02_XB02"
#define R2_VALVE_FILL_FB_CLOSE "1_PK09_XB02"
#define R1_VALVE_DRAIN_FB_CLOSE "1_PK03_XB02"
#define R2_VALVE_DRAIN_FB_CLOSE "1_PK08_XB02"
#define NVD_BYPASS_FB_CLOSE "1_PK01_XB02"

#define R1_PRESSURE_MAIN "1_PT84A_XQ01"
#define R2_PRESSURE_MAIN "1_PT70A_XQ01"
#define R1_PRESSURE_AUX "1_PT66A_XQ01"
#define R2_PRESSURE_AUX "1_PT69A_XQ01"

#define R1_H2_VALVE_MAIN "1_PK04_XC03"
#define R2_H2_VALVE_MAIN "1_PK07_XC03"
#define R1_H2_VALVE_AUX "1_PK05_XC03"
#define R2_H2_VALVE_AUX "1_PK06_XC03"

#define R1_CORE_T1 "1_TE08A_XQ01"
#define R1_CORE_T2 "1_TE09A_XQ01"
#define R1_CORE_T3 "1_TE10A_XQ01"
#define R1_CORE_T4 "1_TE12A_XQ01"
#define R2_CORE_T1 "1_TE18A_XQ01"
#define R2_CORE_T2 "1_TE19A_XQ01"
#define R2_CORE_T3 "1_TE20A_XQ01"
#define R2_CORE_T4 "1_TE22A_XQ01"

#define R1_SHELL_T1 "1_TE13A_XQ01"
#define R1_SHELL_T2 "1_TE14A_XQ01"
#define R1_SHELL_T3 "1_TE15A_XQ01"
#define R1_SHELL_T4 "1_TE17A_XQ01"
#define R2_SHELL_T1 "1_TE23A_XQ01"
#define R2_SHELL_T2 "1_TE24A_XQ01"
#define R2_SHELL_T3 "1_TE25A_XQ01"
#define R2_SHELL_T4 "1_TE27A_XQ01"

typedef struct sched_line_
{
	int time;
	char to_do;
	char ok;
} sched_line_t;

typedef struct reactor_state_
{
	pthread_t tid;
	char reactor_num;
	char busy;
	char valve_fill[60];
	char valve_drain[60];
	char valve_fill_fb_open[60];
	char valve_fill_fb_closed[60];
	char valve_drain_fb_open[60];
	char valve_drain_fb_closed[60];
	char state[60];
	char pressure[60];
	char on_off[60];
	float mass;
	int dwell_time;
	int boil_time;
} reactor_state_t;

reactor_state_t reactor1;
reactor_state_t reactor2;
pthread_mutex_t reactor_sync = PTHREAD_MUTEX_INITIALIZER;


int drain_time(float pressure, float mass)
{
	float g;
	tag_t tag;
	tag = getTag_(DRAIN_TIME_CORRECTION);
	g = 0.00160714286*pressure*pressure - 0.006678571429*pressure + 0.1691428571419;
	return ((mass/g) + tag.value.flt)*1000;
}


int fill_time(float pressure, float mass)
{
	float g0;
	float g;
	g0 = getParm("NVD_L/s");
	g = g0 - 1.11E-3*pressure;
	return (mass/g)*1000;
}

char valve_opacity(char* fb_open, char*fb_close)
{
	tag_t open;
	tag_t close;
	open = getTag_(fb_open);
	close = getTag_(fb_close);
	return open.value.dsc + 1 - close.value.dsc;
}

int valve_operate(char* valve, char* valve_fb_open, char* valve_fb_close, char new_state, int delay)
{
		char before;
		char after;
		tag_t tag;
		int delay_cntr;
		tag = getTag_(valve);
		if(tag.value.dsc != new_state)
		{
			before = valve_opacity(valve_fb_open, valve_fb_close);
			delay_cntr = 0;
			sendTag_(valve, new_state, STATUS_OK);
			for(delay_cntr = 0; delay_cntr < delay; delay_cntr ++)
			{
				delay_ms(500);
				after = valve_opacity(valve_fb_open, valve_fb_close);
				if(new_state && (before < after))
					return delay_cntr + 1;
				if((!new_state) && (before > after))
					return delay_cntr + 1;
			}
			sendTag_(valve, !new_state, STATUS_OK);
			return -1;
		}
		return 0;
}
void stop_reactors(char reactor_num, char reason)
{
	char err_tag[60];
	union
	{
		int i;
		float f;
	} u;
	sendTag_("REACTOR1_RUN",0,STATUS_OK);
	sendTag_("REACTOR2_RUN",0,STATUS_OK);
	if(reactor_num ==2)
	{
		strncpy(err_tag, "REACTOR2_ERROR", 60);
	}
	else
	{
		strncpy(err_tag, "REACTOR1_ERROR", 60);
	}
	u.f = (float)reason;
	sendTag_(err_tag, u.i, STATUS_OK);
	return;
}
void emerg_stop(char reactor_num)
{
	sendTag_(NVD_EMERGENCY_OFF, 0, STATUS_OK);
	if(reactor_num == 1)
	{
		if(reactor1.busy)
			pthread_cancel(reactor1.tid);
		sendTag_("REACTOR1_EMERGENCY", 1, STATUS_OK);
		sendTag_(R1_H2_VALVE_MAIN, 1, STATUS_OK);
		sendTag_(R1_H2_VALVE_AUX, 1, STATUS_OK);
		sendTag_(R1_VALVE_DRAIN, 1, STATUS_OK);
		sendTag_(R1_VALVE_FILL, 0, STATUS_OK);
	}
	else
	{
		if(reactor2.busy)
			pthread_cancel(reactor2.tid);
		sendTag_("REACTOR2_EMERGENCY", 1, STATUS_OK);
		sendTag_(R2_H2_VALVE_MAIN, 1, STATUS_OK);
		sendTag_(R2_H2_VALVE_AUX, 1, STATUS_OK);
		sendTag_(R2_VALVE_DRAIN, 1, STATUS_OK);
		sendTag_(R2_VALVE_FILL, 0, STATUS_OK);
	}
	stop_reactors(1,0);
	return;
}
void drop_error(void)
{
	union
	{
		int i;
		float f;
	} u;
	u.f = 0;
	sendTag_("REACTOR1_ERROR", u.i, STATUS_OK);
	sendTag_("REACTOR2_ERROR", u.i, STATUS_OK);
	return;
}
void unlock_mutex (void * arg)
{
	pthread_mutex_unlock(&reactor_sync);
}

void* reactor_control(void* r)
{
	reactor_state_t * data;
	tag_t fb_ok;
	int delay_cntr;
	tag_t pressure;
	union
	{
		int i;
		float f;
	} u;
	data = r;
	while(1)
	{
		data->busy = 1;
		// Ждем семафора
		u.f = STATE_WAIT_MUTEX;
		sendTag_(data->state, u.i, STATUS_OK);
		pthread_mutex_lock(&reactor_sync);
		pthread_cleanup_push(&unlock_mutex, NULL);
		// Дождались семафора, надо ли наполнять реактор?
		fb_ok = getTag_(data->on_off);
		if(fb_ok.value.dsc == 0)
		{
			data->busy = 0;
			u.f = STATE_OFF;
			sendTag_(data->state, u.i, STATUS_OK);
			return NULL;
		}
		// Надо. Достаточен ли уровень в смесителе?
		u.f = STATE_WAIT_MIXER;
		sendTag_(data->state, u.i, STATUS_OK);
		do
		{
			fb_ok = getTag_(data->on_off);
			if(fb_ok.value.dsc == 0)
			{
				data->busy = 0;
				u.f = STATE_OFF;
				sendTag_(data->state, u.i, STATUS_OK);
				return NULL;
			}
			fb_ok = getTag_("1_LE149A_XQ01"); 
			if(fb_ok.value.flt < data->mass + 5)
				delay_ms(5000);
		}
		while(fb_ok.value.flt < data->mass + 5); 
		//Наполнение реактора
		u.f = STATE_FILL;
		sendTag_(data->state, u.i, STATUS_OK);
		//Пытаемся открыть клапан впрыска
		delay_cntr = valve_operate(data->valve_fill,
					  data->valve_fill_fb_open,
					  data->valve_fill_fb_closed,
					  1, 20);
		if(delay_cntr < 0)
		{
			stop_reactors(data->reactor_num, STOP_REASON_FILL_VALVE_JAMMED);
			data->busy = 0;
			u.f = STATE_OFF;
			sendTag_(data->state, u.i, STATUS_OK);
			return NULL;
		}
		// Впрыскиваем
		sendTag_(NVD_VALVE, 1, STATUS_OK);
		delay_ms(500);
		sendTag_(NVD_RELAY, 1, STATUS_OK);
		pressure = getTag_(data->pressure);		
		if(pressure.state == STATUS_OK)
		{
			delay_ms(fill_time(pressure.value.flt, data->mass));
		}
		else
		{
			delay_ms(getParm("REACTOR_BLIND_FILL_TIME_S")*1000);
		}
		sendTag_(NVD_VALVE, 0, STATUS_OK);
		// Промываем
		sendTag_(NVD_FLUSH, 1, STATUS_OK);
		printf("DELAY  %e\n",getParm("NVD_FLUSH_VOL_L")/getParm("NVD_L/s")*1000+300);
		delay_ms(getParm("NVD_FLUSH_VOL_L")/getParm("NVD_L/s")*1000+300);
		printf("DELAY_OK\n");
		// Отключаем НВД
		sendTag_(NVD_RELAY, 0, STATUS_OK);
		delay_cntr = 0;
		do
		{
			delay_ms(500);
			fb_ok = getTag_(NVD_FB);
			if(delay_cntr >= 10)
			{
				sendTag_(NVD_EMERGENCY_OFF, 0, STATUS_OK);
				sendTag_(NVD_VALVE, 0, STATUS_OK);
				stop_reactors(data->reactor_num, STOP_REASON_NVD_ERROR);
				break;
			}
			delay_cntr ++;
		}
		while(fb_ok.value.dsc == 1);
		delay_ms(500);
		sendTag_(NVD_FLUSH, 0, STATUS_OK);
		// Закрываем клапан впрыска
		delay_cntr = valve_operate(data->valve_fill,
								   data->valve_fill_fb_open,
								   data->valve_fill_fb_closed,
								   0, 20);
		if(delay_cntr < 0)
			stop_reactors(data->reactor_num, STOP_REASON_FILL_VALVE_JAMMED);
		//Разрешаем наполнение другого реактора
		pthread_cleanup_pop(1);
		u.f = STATE_BOIL;
		sendTag_(data->state, u.i, STATUS_OK);
		//Реактор работает, ждем
		delay_ms(data->boil_time - 500*delay_cntr);
		//Сброс
		u.f = STATE_DRAIN;
		sendTag_(data->state, u.i, STATUS_OK);
		// Открываем клапан сброса
		delay_cntr = valve_operate(data->valve_drain,
					  data->valve_drain_fb_open,
					  data->valve_drain_fb_closed,
					  1, 20);
		if(delay_cntr <0)
		{	
			stop_reactors(data->reactor_num, STOP_REASON_DRAIN_VALVE_JAMMED);
			data->busy = 0;
			u.f = STATE_OFF;
			sendTag_(data->state, u.i, STATUS_OK);
			return NULL;
		}
		// Время сброса
		pressure = getTag_(data->pressure);
		if(pressure.state ==0)
		{
			delay_ms(drain_time(pressure.value.flt, data->mass));
		}
		else
		{
			fb_ok = getTag_(DRAIN_TIME_CORRECTION);
			delay_ms((getParm("REACTOR_BLIND_DRAIN_TIME_S") + fb_ok.value.flt)*1000);
		}
		
		// Закрываем клапан сброса
		delay_cntr = valve_operate(data->valve_drain,
					  data->valve_drain_fb_open,
					  data->valve_drain_fb_closed,
					  0, 20);
		if(delay_cntr <0)
		{	
			stop_reactors(data->reactor_num, STOP_REASON_DRAIN_VALVE_JAMMED);
			data->busy = 0;
			u.f = STATE_OFF;
			sendTag_(data->state, u.i, STATUS_OK);
			return NULL;
		}
		// Время DWELL
		u.f = STATE_DWELL;
		sendTag_(data->state, u.i, STATUS_OK);
		if(data->dwell_time - 500*delay_cntr > 0)
			delay_ms(data->dwell_time - 500*delay_cntr);
		drop_error();
	}
	return NULL;
}

int flush(void)
{
	tag_t fb_ok;
	int delay_cntr = 0;
	if(pthread_mutex_trylock(&reactor_sync))
	{
		return 0;
	}
	sendTag_("NVD_BYPASS_STATE", 1, STATUS_OK);
	//Уровень в смесителе
	do
	{
		fb_ok = getTag_("REACTOR1_RUN");
		if(fb_ok.value.dsc == 0)
		{
			fb_ok = getTag_("REACTOR2_RUN");
			if(fb_ok.value.dsc == 0)
			{	
				pthread_mutex_unlock(&reactor_sync);
				sendTag_("NVD_BYPASS_STATE", 0, STATUS_OK);
				return -1;
			}
		}
		fb_ok = getTag_("1_LE149A_XQ01"); 
		if(fb_ok.value.flt < 11)
			delay_ms(5000);
	}
	while(fb_ok.value.flt < 11); 
	
	//Пытаемся открыть клапан байпаса
	delay_cntr = valve_operate(NVD_BYPASS, NVD_BYPASS_FB_OPEN, NVD_BYPASS_FB_CLOSE, 1, 20);
	if(delay_cntr < 0)
	{
		pthread_mutex_unlock(&reactor_sync);
		sendTag_("NVD_BYPASS_STATE", 0, STATUS_OK);
		return -1;
	}
	// Работа на байпас
	sendTag_(NVD_VALVE, 1, STATUS_OK);
	delay_ms(500);
	sendTag_(NVD_RELAY, 1, STATUS_OK);
	delay_ms(getParm("NVD_BYPASS_TIME_S")*1000);
	sendTag_(NVD_RELAY, 0, STATUS_OK);
	delay_cntr = 0;
	do
	{
		delay_ms(500);
		fb_ok = getTag_(NVD_FB);
		if(delay_cntr >= 10)
		{
			sendTag_(NVD_EMERGENCY_OFF, 0, STATUS_OK);
			sendTag_(NVD_VALVE, 0, STATUS_OK);
			sendTag_("NVD_BYPASS_STATE", 0, STATUS_OK);
			pthread_mutex_unlock(&reactor_sync);
			return -1;
		}
		delay_cntr ++;
	}
	while(fb_ok.value.dsc == 1);
	delay_ms(500);
	sendTag_(NVD_VALVE, 0, STATUS_OK);
	// Закрываем клапан байпаса
	delay_cntr = valve_operate(NVD_BYPASS, NVD_BYPASS_FB_OPEN, NVD_BYPASS_FB_CLOSE, 0, 20);
	if(delay_cntr < 0)
	{
		pthread_mutex_unlock(&reactor_sync);
		sendTag_("NVD_BYPASS_STATE", 0, STATUS_OK);
		return -1;
	}
	pthread_mutex_unlock(&reactor_sync);
	sendTag_("NVD_BYPASS_STATE", 0, STATUS_OK);
	return 0;
}

void * start_reactor1(void * argRef)
{
	relayArgs_t arg;
	tag_t r1_run;
	tag_t r2_run;
	pthread_attr_t threadAttr;
	arg = *((relayArgs_t*)argRef);
	if(arg.firstRun)
	{
		addTag_("REACTOR1_RUN",0,TYPE_DIGITAL,arg.n,SUBSCRIBE);
		addTag_("REACTOR2_RUN",0,TYPE_DIGITAL,arg.n, NOT_SUBSCRIBE);
		addTag_("REACTOR1_STATE", 0, TYPE_REAL, arg.n, NOT_SUBSCRIBE);
		addTag_("NVD_BYPASS_STATE", 0, TYPE_DIGITAL, arg.n, NOT_SUBSCRIBE);
		addTag_(R1_VALVE_DRAIN,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R1_VALVE_FILL,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R1_VALVE_DRAIN_FB_OPEN,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R1_VALVE_FILL_FB_OPEN,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R1_VALVE_DRAIN_FB_CLOSE,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R1_VALVE_FILL_FB_CLOSE,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R1_PRESSURE_MAIN,0,TYPE_REAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R1_PRESSURE_AUX, 0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);

		addTag_(NVD_RELAY,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(NVD_FB,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(NVD_EMERGENCY_OFF,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(NVD_VALVE,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(NVD_FLUSH,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);

		addTag_(NVD_BYPASS,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(NVD_BYPASS_FB_OPEN,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(NVD_BYPASS_FB_CLOSE,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);

		addTag_(DRAIN_TIME_CORRECTION,0,TYPE_REAL, arg.n, NOT_SUBSCRIBE);

		addTag_("REACTOR1_ERROR",0,TYPE_REAL, arg.n, NOT_SUBSCRIBE);
		addTag_("REACTOR2_ERROR",0,TYPE_REAL, arg.n, NOT_SUBSCRIBE);
	}
	else
	{
		r1_run = getTag_("REACTOR1_RUN");
		r2_run = getTag_("REACTOR2_RUN");
		reactor1.mass = getParm("REACTOR1_LOAD_MASS");
		reactor1.boil_time = 1000*getParm("REACTOR_BOIL_TIME_S");
		reactor1.dwell_time = 1000*getParm("REACTOR1_DWELL_TIME_S");
		reactor1.reactor_num = 1;

		strncpy(reactor1.pressure, R1_PRESSURE_MAIN, 60);
		strncpy(reactor1.on_off, "REACTOR1_RUN", 60);
		strncpy(reactor1.state , "REACTOR1_STATE", 60);
		strncpy(reactor1.valve_drain, R1_VALVE_DRAIN, 60);
		strncpy(reactor1.valve_drain_fb_closed, R1_VALVE_DRAIN_FB_CLOSE, 60);
		strncpy(reactor1.valve_drain_fb_open, R1_VALVE_DRAIN_FB_OPEN, 60);
		strncpy(reactor1.valve_fill, R1_VALVE_FILL, 60);
		strncpy(reactor1.valve_fill_fb_closed, R1_VALVE_FILL_FB_CLOSE, 60);
		strncpy(reactor1.valve_fill_fb_open, R1_VALVE_FILL_FB_OPEN, 60);

		if( r1_run.value.dsc || r2_run.value.dsc
				&& (reactor1.busy == 0)
				&& (reactor2.busy == 0))
		{
			if (flush())
			{
				stop_reactors(1, STOP_REASON_BYPASS);
				dropFunc_(argRef);
				return NULL;
			}
		}	
		if(r1_run.value.dsc && (reactor1.busy == 0))
		{
			pthread_attr_init(&threadAttr);
			pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_DETACHED);
			pthread_create(&(reactor1.tid),&threadAttr, reactor_control, &reactor1);
		}
	}
	dropFunc_(argRef);
	return NULL;
}

void * start_reactor2(void * argRef)
{
	relayArgs_t arg;
	tag_t r1_run;
	tag_t r2_run;
	pthread_attr_t threadAttr;
	arg = *((relayArgs_t*)argRef);
	if(arg.firstRun)
	{
		addTag_("REACTOR2_RUN",0,TYPE_DIGITAL,arg.n,SUBSCRIBE);
		addTag_("REACTOR1_RUN",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_("REACTOR2_STATE", 0, TYPE_REAL, arg.n, NOT_SUBSCRIBE);
		addTag_("NVD_BYPASS_STATE", 0, TYPE_DIGITAL, arg.n, NOT_SUBSCRIBE);

		addTag_(R2_VALVE_FILL,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R2_VALVE_DRAIN,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R2_VALVE_FILL_FB_OPEN,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R2_VALVE_DRAIN_FB_OPEN,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R2_VALVE_FILL_FB_CLOSE,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R2_VALVE_DRAIN_FB_CLOSE,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R2_PRESSURE_MAIN,1,TYPE_REAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R2_PRESSURE_AUX, 0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);

		addTag_(NVD_RELAY,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(NVD_FB,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(NVD_EMERGENCY_OFF,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(NVD_VALVE,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(NVD_FLUSH,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);

		addTag_(NVD_BYPASS,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(NVD_BYPASS_FB_OPEN,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(NVD_BYPASS_FB_CLOSE,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		
		addTag_(DRAIN_TIME_CORRECTION,0,TYPE_REAL, arg.n, NOT_SUBSCRIBE);

		addTag_("REACTOR1_ERROR",0,TYPE_REAL, arg.n, NOT_SUBSCRIBE);
		addTag_("REACTOR2_ERROR",0,TYPE_REAL, arg.n, NOT_SUBSCRIBE);
	}
	else
	{
		r1_run = getTag_("REACTOR1_RUN");
		r2_run = getTag_("REACTOR2_RUN");

		reactor2.mass = getParm("REACTOR2_LOAD_MASS");
		reactor2.boil_time = reactor1.boil_time;
		reactor2.dwell_time = 1000*getParm("REACTOR2_DWELL_TIME_S");
		reactor2.reactor_num = 2;

		strncpy(reactor2.pressure, R2_PRESSURE_MAIN, 60);
		strncpy(reactor2.on_off, "REACTOR2_RUN", 60);
		strncpy(reactor2.state , "REACTOR2_STATE", 60);
		strncpy(reactor2.valve_drain, R2_VALVE_DRAIN, 60);
		strncpy(reactor2.valve_drain_fb_closed, R2_VALVE_DRAIN_FB_CLOSE, 60);
		strncpy(reactor2.valve_drain_fb_open, R2_VALVE_DRAIN_FB_OPEN, 60);
		strncpy(reactor2.valve_fill, R2_VALVE_FILL, 60);
		strncpy(reactor2.valve_fill_fb_closed, R2_VALVE_FILL_FB_CLOSE, 60);
		strncpy(reactor2.valve_fill_fb_open, R2_VALVE_FILL_FB_OPEN, 60);

		if( r1_run.value.dsc || r2_run.value.dsc
				&& (reactor1.busy == 0)
				&& (reactor2.busy == 0))
		{
			if (flush())
			{
				stop_reactors(1, STOP_REASON_BYPASS);
				dropFunc_(argRef);
				return NULL;
			}
			
		}	
		if(r2_run.value.dsc && (reactor2.busy == 0))
		{
			pthread_attr_init(&threadAttr);
			pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_DETACHED);
			pthread_create(&(reactor1.tid),&threadAttr, reactor_control, &reactor2);
		}
	}
	dropFunc_(argRef);
	return NULL;
}

void * pneumatics_chk(void * argRef)
{
	tag_t r1_run;
	tag_t r2_run;
	tag_t ramp;
	int e;
	union
	{
		int i;
		float f;
	} u;
	relayArgs_t arg;
	arg = *((relayArgs_t*)argRef);
	if(arg.firstRun)
	{
		addTag_("1_PT120A_XQ01",0,TYPE_REAL,arg.n,SUBSCRIBE);
		addTag_("REACTOR1_RUN",0,TYPE_DIGITAL,arg.n,SUBSCRIBE);
		addTag_("REACTOR2_RUN",0,TYPE_DIGITAL,arg.n,SUBSCRIBE);
	}
	else
	{
		r1_run = getTag_("REACTOR1_RUN");
		r2_run = getTag_("REACTOR2_RUN");
		ramp = getTag_("1_PT120A_XQ01");
		if((r1_run.value.dsc || r2_run.value.dsc) && (ramp.value.flt < 5))
		{
			stop_reactors(1, STOP_REASON_PNEUMATICS_LOW_PRESSURE);
		}
		if(ramp.value.flt > 5)
		{
			ramp = getTag_("REACTOR1_ERROR");
			e = ramp.value.flt;
			e &= ~STOP_REASON_PNEUMATICS_LOW_PRESSURE;
			u.f = e;
			sendTag_("REACTOR1_ERROR",u.i, STATUS_OK);
		}
	}
	dropFunc_(argRef);
	return NULL;
}
void * r_thermal_shutdown(void * argRef)
{
	tag_t r1t[4];
	tag_t r2t[4];
	int i;
	float tmax;
	relayArgs_t arg;
	arg = *((relayArgs_t*)argRef);
	if(arg.firstRun)
	{
		addTag_(R1_CORE_T1,1,TYPE_REAL,arg.n,SUBSCRIBE);
		addTag_(R1_CORE_T2,1,TYPE_REAL,arg.n,SUBSCRIBE);
		addTag_(R1_CORE_T3,1,TYPE_REAL,arg.n,SUBSCRIBE);
		addTag_(R1_CORE_T4,1,TYPE_REAL,arg.n,SUBSCRIBE);
		addTag_(R2_CORE_T1,0,TYPE_REAL,arg.n,SUBSCRIBE);
		addTag_(R2_CORE_T2,1,TYPE_REAL,arg.n,SUBSCRIBE);
		addTag_(R2_CORE_T3,1,TYPE_REAL,arg.n,SUBSCRIBE);
		addTag_(R2_CORE_T4,0,TYPE_REAL,arg.n,SUBSCRIBE);

		addTag_("REACTOR1_EMERGENCY",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_("REACTOR2_EMERGENCY",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R1_H2_VALVE_AUX,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R2_H2_VALVE_AUX,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);	
		addTag_(R1_H2_VALVE_MAIN,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R2_H2_VALVE_MAIN,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);	
		addTag_(R1_VALVE_DRAIN,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R2_VALVE_DRAIN,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R1_VALVE_FILL,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R2_VALVE_FILL,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(NVD_EMERGENCY_OFF,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
	}
	else
	{
		r1t[0] = getTag_(R1_CORE_T1);
		r1t[1] = getTag_(R1_CORE_T2);
		r1t[2] = getTag_(R1_CORE_T3);
		r1t[3] = getTag_(R1_CORE_T4);

		r2t[0] = getTag_(R2_CORE_T1);
		r2t[1] = getTag_(R2_CORE_T2);
		r2t[2] = getTag_(R2_CORE_T3);
		r2t[3] = getTag_(R2_CORE_T4);
		tmax = 0;
		for(i = 0; i < 4; i++)
		{
			if(r1t[i].state == STATUS_OK)
				tmax = max(tmax, r1t[i].value.flt);
		}
		if(tmax > 300)
		{
			emerg_stop(1);
		}
		tmax = 0;
		for(i = 0; i < 4; i++)
		{
			if(r2t[i].state == STATUS_OK)
			{	
				if(r2t[i].value.flt > tmax)
					tmax = r2t[i].value.flt;
			}
		}
		if(tmax > 300)
		{
			emerg_stop(2);
		}
	}
	dropFunc_(argRef);
	return NULL;
}

void * r_H2_pressure(void * argRef)
{
	tag_t run[2];
	tag_t main_p[2];
	tag_t aux_p[2];
	tag_t valve[2];
	float pressure;
	int i = 0;
	relayArgs_t arg;
	arg = *((relayArgs_t*)argRef);


	if(arg.firstRun)
	{
		addTag_("REACTOR1_RUN",0,TYPE_DIGITAL,arg.n,SUBSCRIBE);
		addTag_("REACTOR2_RUN",0,TYPE_DIGITAL,arg.n,SUBSCRIBE);
		addTag_(R1_PRESSURE_MAIN,0,TYPE_REAL,arg.n,SUBSCRIBE);
		addTag_(R2_PRESSURE_MAIN,1,TYPE_REAL,arg.n,SUBSCRIBE);
		addTag_(R1_PRESSURE_AUX,0,TYPE_REAL,arg.n,SUBSCRIBE);
		addTag_(R2_PRESSURE_AUX,0,TYPE_REAL,arg.n,SUBSCRIBE);
		
		addTag_(R1_H2_VALVE_MAIN,0,TYPE_REAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R2_H2_VALVE_MAIN,0,TYPE_REAL,arg.n,NOT_SUBSCRIBE);
		addTag_("REACTOR1_EMERGENCY",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_("REACTOR2_EMERGENCY",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R1_H2_VALVE_AUX,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R2_H2_VALVE_AUX,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);	
		addTag_(R1_VALVE_DRAIN,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R2_VALVE_DRAIN,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R1_VALVE_FILL,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(R2_VALVE_FILL,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_(NVD_EMERGENCY_OFF,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);	


	}
	else
	{
		run[0] = getTag_("REACTOR1_RUN");
		run[1] = getTag_("REACTOR2_RUN");
		main_p[0] = getTag_(R1_PRESSURE_MAIN);
		main_p[1] = getTag_(R2_PRESSURE_MAIN);
		aux_p[0] = getTag_(R1_PRESSURE_AUX);
		aux_p[1] = getTag_(R2_PRESSURE_AUX);
		valve[0] = getTag_(R1_H2_VALVE_MAIN);
		valve[1] = getTag_(R2_H2_VALVE_MAIN);
		for(i = 0; i <2; i++)
		{
			if(max(main_p[i].value.flt,aux_p[i].value.flt) > 23)
				emerg_stop(i+1);
			if(run[i].value.dsc)
			{
				if(main_p[i].state == STATUS_OK)
				{
					pressure = main_p[i].value.flt;
				}
				else
				{
					if(aux_p[i].state == STATUS_OK)
					{
						pressure = aux_p[i].value.flt;
					}
					else
					{
						pressure = 10000;
					}
				}
				if((pressure > getParm("REACTOR_PRESSURE_MAX")) && valve[i].value.dsc == 0)
					sendTag_(valve[i].name, 1, STATUS_OK);
				if(pressure < getParm("REACTOR_PRESSURE_MIN")  && valve[i].value.dsc != 0)
					sendTag_(valve[i].name, 0, STATUS_OK);
			}
		}
	}
	dropFunc_(argRef);
	return NULL;
}
void * NVD_overpressure(void * argRef)
{
	tag_t p;
	relayArgs_t arg;
	int e;
	union
	{
		int i;
		float f;
	} u;
	arg = *((relayArgs_t*)argRef);
	if(arg.firstRun)
	{
		addTag_("REACTOR1_RUN",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_("REACTOR2_RUN",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);
		addTag_("1_PT119A_XQ01",0,TYPE_REAL,arg.n,SUBSCRIBE);
		addTag_(NVD_EMERGENCY_OFF,0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);	
	}
	else
	{
		p = getTag_("1_PT119A_XQ01");
		if((p.value.flt > 23) ||(p.state != STATUS_OK))
		{
			sendTag_(NVD_EMERGENCY_OFF, 0, STATUS_OK);
			stop_reactors(1, STOP_REASON_NVD_HIGH_PRESURE);
		}
		else
		{
			p = getTag_("REACTOR1_ERROR");
			e = p.value.flt;
			e &= ~STOP_REASON_NVD_HIGH_PRESURE;
			u.f = e;
			sendTag_("REACTOR1_ERROR",u.i, STATUS_OK);
		}
	}
	dropFunc_(argRef);
	return NULL;
}
void * do_autolevel(void * argRef)
{
	hysteresis_w_enable(argRef,"1_LE159A_XQ01", 0, "1_PK15_XC03",0 ,"PK15_AUTO", 0, getParm("DO_MIN_L"), getParm("DO_MAX_L"),FRIGE);
	return NULL;
}
void * vo_autolevel(void * argRef)
{
	
	linear(argRef, 
		   "1_PIS161A_XQ01", 0, 
		   "1_RU04_XC01", 0, 
		   "1_RU04_XC02", 0, 
		   "1_RU04_XQ01", 0, 
		   "RU4_AUTO", 0, 
		   getParm("VO_MIN"), getParm("VO_MAX"), 
		   getParm("RU4_MAX"), getParm("RU4_HYST"));
	return NULL;
}

void * k21_autolevel(void * argRef)
{
	
	linear(argRef, 
		   "1_PIS163A_XQ01", 0, 
		   "1_RU01_XC01", 0, 
		   "1_RU01_XC02", 0, 
		   "1_RU01_XQ01", 0, 
		   "RU1_AUTO", 0, 
		   getParm("K21_MIN"), getParm("K21_MAX"), 
		   getParm("RU1_MAX"), getParm("RU1_HYST"));
	return NULL;
}
void * dg_autolevel(void * argRef)
{
	hysteresis_w_enable(argRef,"1_LE150A_XQ01", 0, "1_EMK05_XC03",0 ,"DG_AUTOLEVEL", 0, getParm("DG_MIN_L"), getParm("DG_MAX_L"),FRIGE);
	return NULL;
}
void * ramp_autopressure(void * argRef)
{
	hysteresis_w_enable(argRef,"1_PT108A_XQ01", 0, "1_KSV_XC03",0 ,"KSV_AUTOPRESSURE", 0, getParm("RAMP_MIN_MPA"), getParm("RAMP_MAX_MPA"),HEATER);
	return NULL;
}


void init_lib(int libno)
{
	reactor1.busy = 0;
	reactor2.busy = 0;
	initAction_(libno, "pneumatics_chk", RUN_MODAL);
	initAction_(libno, "start_reactor1",RUN_THREAD);
	initAction_(libno, "start_reactor2",RUN_THREAD);
	initAction_(libno, "r_H2_pressure",RUN_MODAL);
	initAction_(libno, "r_thermal_shutdown", RUN_MODAL);
	initAction_(libno, "NVD_overpressure", RUN_MODAL);
	initAction_(libno, "do_autolevel", RUN_MODAL);
	initAction_(libno, "dg_autolevel", RUN_MODAL);
	initAction_(libno, "vo_autolevel", RUN_MODAL);
	initAction_(libno, "k21_autolevel", RUN_MODAL);
	initAction_(libno, "ramp_autopressure", RUN_MODAL);
	
//	initAction(SHD_sensefail, RUN_MODAL);
//	initAction(pump_pwr_sensefail, RUN_MODAL);
//	initAction(bust_mix_autolevel, RUN_MODAL);
//initAction(f2,RUN_MODAL);
	return;
}

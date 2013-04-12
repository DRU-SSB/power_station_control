#include <stdlib.h>
#include <stdio.h>
#include <../relay/relay_defs.h>
#include <time.h>
#include <unistd.h>
#include <../stdcyclo/stdcyclo.h>
#include <math.h>
#include <pthread.h>
#define RO_AL 2.7

void* mix_autolevel_arg = NULL;

void * bov_autolevel(void * argRef)
{
	hysteresis_w_enable(argRef,"1_LE146A_XQ01", 0, "1_VPU_XC03",0 ,"BOV_AUTOLEVEL", 0, 200, 600,FRIGE);
	return NULL;
}
void * mixer_control(void * argRef)
{
	tag_t mix_level;
	tag_t bov_level;
	tag_t mix_autolevel;
	tag_t ndv_power_on;
	tag_t shd_power_on;
	tag_t pis63;
	tag_t ndv_err;
	tag_t shd_err;
	relayArgs_t arg;
	float alpha;
	long NDV_work_ms;
	long SHD_work_ms;
	float susp_required;
	arg = *((relayArgs_t*)argRef);
	if(arg.firstRun) 
	{
		addTag_("MIXER_AUTOLEVEL",0,TYPE_DIGITAL,arg.n,SUBSCRIBE); //Включение автомата поддержания уровня в смесителе
		addTag_("1_LE149A_XQ01",0,TYPE_REAL,arg.n,SUBSCRIBE);  // Уровень в смесителе
		addTag_("1_LE146A_XQ01",0,TYPE_REAL,arg.n,NOT_SUBSCRIBE); // Уровень в БОВ
		addTag_("1_PWR_SCHD_XC03",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // Реле ШД
		addTag_("1_NDV_XC04",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // Реле НДВ
		addTag_("1_SCHD_XG21",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // Питание на ШД подано
		addTag_("1_NDV_XB01",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // Питание на НДВ подано
		addTag_("1_EMK01_XC03",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // Электромагнитный клапан
		addTag_("1_PIS63_XH01",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE); //ЭКМ после НДВ
		addTag_("1_NDV_XB02",0,TYPE_DIGITAL,arg.n, NOT_SUBSCRIBE); // обратная связь НДВ
		addTag_("1_SCHD_XB03",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE); // авария ШД
	}
	else
	{
		mix_level = getTag_("1_LE149A_XQ01");
		bov_level = getTag_("1_LE146A_XQ01");
		mix_autolevel = getTag_("MIXER_AUTOLEVEL");
		ndv_power_on = getTag_("1_NDV_XB01");
		shd_power_on = getTag_("1_SCHD_XG21");
		pis63 = getTag_("1_PIS63_XH01");
		shd_err = getTag_("1_SCHD_XB03");
		ndv_err = getTag_("1_NDV_XB02");		
		// А надо ли работать?
		if(mix_autolevel.value.dsc == 0)
		{
			dropFunc_(argRef);
			return NULL;
		}		
		// Включено ли питание насосов?
		if((ndv_power_on.value.dsc && shd_power_on.value.dsc) == 0)
		{
			sendTag_("MIXER_AUTOLEVEL", 0, STATUS_OK);
			dropFunc_(argRef);
			return NULL;
		}
		// Не установлены ли ошибки насосов
		if(ndv_err.value.dsc || (shd_err.value.dsc == 0))
		{
			sendTag_("MIXER_AUTOLEVEL", 0, STATUS_OK);
			dropFunc_(argRef);
			return NULL;
		}
		// Нормальное ли давление?
		if(pis63.value.dsc)
		{
			sendTag_("MIXER_AUTOLEVEL", 0, STATUS_OK);
			dropFunc_(argRef);
			return NULL;
		}
		// Расчитываем время включения насосов
		susp_required = getParm("SUSPENSION_MAX_LEVEL") - mix_level.value.flt;
		alpha = 1/(getParm("STOCHIOMETRY_AL/H2O") + 1); 
		NDV_work_ms = susp_required*alpha/getParm("NDV_L/s")*1000;
		SHD_work_ms = susp_required*(1-alpha)/getParm("SHD_kg/s")*1000;

		// Исправен ли датчик уровня в смесителе?
		if(mix_level.state != STATUS_OK)
		{
			dropFunc_(argRef);
			return NULL;
		}
		
		// Не слишком ли мало суспензии нужно приготовить?
		if(susp_required < 10) 
		{
			dropFunc_(argRef);
			return NULL;
		}
		// Хватит ли воды в БОВ?	
		if(susp_required > bov_level.value.flt)
		{
			dropFunc_(argRef);
			return NULL;
		}
		mix_autolevel_arg = argRef;
		// Наливаем
		printf("Turning on PUMPS:\n");
		printf("SHD: %ld ms\nNDV: %ld ms\n", SHD_work_ms, NDV_work_ms);
		printf("N = %f\n", getParm("STOCHIOMETRY_AL/H2O"));
		if(NDV_work_ms > SHD_work_ms)
		{
			sendTag_("1_EMK01_XC03",1,STATUS_OK);
			delay_ms(1000);
			sendTag_("1_NDV_XC04", 1, STATUS_OK);
			sendTag_("1_PWR_SCHD_XC03", 1, STATUS_OK);
			delay_ms(SHD_work_ms);
			sendTag_("1_PWR_SCHD_XC03", 0, STATUS_OK);
			delay_ms(NDV_work_ms - SHD_work_ms);
			sendTag_("1_NDV_XC04", 0, STATUS_OK);
			delay_ms(1000);
			sendTag_("1_EMK01_XC03",0,STATUS_OK);
		}
		else
		{
			sendTag_("1_EMK01_XC03",1,STATUS_OK);
			delay_ms(1000);
			sendTag_("1_NDV_XC04", 1, STATUS_OK);
			sendTag_("1_PWR_SCHD_XC03", 1, STATUS_OK);
			delay_ms(NDV_work_ms);
			sendTag_("1_NDV_XC04", 0, STATUS_OK);
			if(SHD_work_ms - NDV_work_ms<1000)
			{
				delay_ms(SHD_work_ms - NDV_work_ms);
				sendTag_("1_PWR_SCHD_XC03", 0, STATUS_OK);
				delay_ms(1000 - (SHD_work_ms - NDV_work_ms));
				sendTag_("1_EMK01_XC03",0,STATUS_OK);
			}
			else
			{
				delay_ms(1000);
				sendTag_("1_EMK01_XC03",0,STATUS_OK);
				delay_ms(SHD_work_ms - NDV_work_ms - 1000);
				sendTag_("1_PWR_SCHD_XC03", 0, STATUS_OK);
			}
		}
	}
	mix_autolevel_arg = NULL;
	dropFunc_(argRef);
	return NULL;
}
void * mixer_onoff(void * argRef)
{
	tag_t mix_level;
	tag_t mix_power_on;
	tag_t mix_autolevel;
	relayArgs_t arg;
	arg = *((relayArgs_t*)argRef);
	if(arg.firstRun)
	{
		addTag_("MIXER_AUTOLEVEL",0,TYPE_DIGITAL,arg.n,SUBSCRIBE); //Включение автомата поддержания уровня в смесителе
		addTag_("1_LE149A_XQ01",0,TYPE_REAL,arg.n,SUBSCRIBE);  // Уровень в смесителе
		addTag_("1_E01_XC03",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE); // реле смесителя
	}
	else
	{
		mix_autolevel = getTag_("MIXER_AUTOLEVEL");
		mix_power_on = getTag_("1_E01_XC03");
		mix_level = getTag_("1_LE149A_XQ01");
		// А надо ли работать?
		if(mix_autolevel.value.dsc == 0)
		{
			dropFunc_(argRef);
			return NULL;
		}
		if(mix_level.value.flt > 10)
		{
			if(mix_power_on.value.dsc == 0)
			{
				sendTag_("1_E01_XC03", 1, STATUS_OK);
			}
		}
		else
		{
			if(mix_power_on.value.dsc)
			{
				sendTag_("1_E01_XC03", 0, STATUS_OK);
			}
		}
	}
	dropFunc_(argRef);
	return NULL;
}
void * NDV_overpressure(void * argRef)
{
	tag_t pis63;
	relayArgs_t arg;
	arg = *((relayArgs_t*)argRef);
	if(arg.firstRun)
	{
		addTag_("1_PIS63_XH01",0,TYPE_DIGITAL,arg.n,SUBSCRIBE); //ЭКМ после НДВ
		addTag_("1_PWR_SCHD_XC03",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // Реле ШД
		addTag_("1_NDV_XC04",0,TYPE_DIGITAL,arg.n, SUBSCRIBE);  // Реле НДВ
		addTag_("MIXER_AUTOLEVEL",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE); //Включение автомата поддержания уровня в смесителе
		addTag_("1_EMK01_XC03",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // Электромагнитный клапан
	}
	else
	{
		pis63 = getTag_("1_PIS63_XH01");
		if(pis63.value.dsc)
		{
			sendTag_("1_NDV_XC04", 0, STATUS_OK);
			sendTag_("1_PWR_SCHD_XC03", 0, STATUS_OK);
			sendTag_("1_EMK01_XC03", 0, STATUS_OK);
			sendTag_("MIXER_AUTOLEVEL", 0, STATUS_OK);
		}
	}
	dropFunc_(argRef);
	return NULL;
	
}
void * NDV_sensefail(void * argRef)
{
	tag_t NDV_fb;
	tag_t NDV_relay;
	relayArgs_t arg;
	arg = *((relayArgs_t*)argRef);
	if(arg.firstRun)
	{
		addTag_("1_NDV_XB02",0,TYPE_DIGITAL,arg.n, SUBSCRIBE); // обратная связь НДВ
		addTag_("1_NDV_XB01",0,TYPE_DIGITAL,arg.n, NOT_SUBSCRIBE);  // Питание на НДВ подано

		addTag_("1_NDV_XC04",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // Реле ШД
		addTag_("1_PWR_SCHD_XC03",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // Реле ШД
		addTag_("MIXER_AUTOLEVEL",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE); //Включение автомата поддержания уровня в смесителе
		addTag_("1_EMK01_XC03",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // Электромагнитный клапан
		addTag_("NDV_ERROR",0,TYPE_REAL,arg.n,NOT_SUBSCRIBE);
	}
	else
	{
		NDV_relay = getTag_("1_NDV_XB01");
		NDV_fb = getTag_("1_NDV_XB02");
		if(NDV_relay.value.dsc && NDV_fb.value.dsc)
		{
			sendTag_("1_NDV_XC04", 0, STATUS_OK);
			sendTag_("1_PWR_SCHD_XC03", 0, STATUS_OK);
			sendTag_("1_EMK01_XC03", 0, STATUS_OK);
			sendTag_("MIXER_AUTOLEVEL", 0, STATUS_OK);
			sendTag_("NDV_ERROR", 1, STATUS_OK);	
		}
		else
		{
			sendTag_("NDV_ERROR", 0, STATUS_OK);
		}
	}
	dropFunc_(argRef);
	return NULL;
}
void * SHD_sensefail(void * argRef)
{
	tag_t SHD_fb;
	tag_t SHD_relay;
	relayArgs_t arg;
	arg = *((relayArgs_t*)argRef);
	if(arg.firstRun)
	{
		addTag_("1_SCHD_XB03",0,TYPE_DIGITAL,arg.n,SUBSCRIBE); // авария ШД
		addTag_("1_SCHD_XG21",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // Питание на ШД подано

		addTag_("1_NDV_XC04",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // реле НДВ
		addTag_("1_PWR_SCHD_XC03",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // Реле ШД
		addTag_("MIXER_AUTOLEVEL",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE); //Включение автомата поддержания уровня в смесителе
		addTag_("1_EMK01_XC03",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // Электромагнитный клапан
		addTag_("SHD_ERROR",0,TYPE_REAL,arg.n,NOT_SUBSCRIBE);
	}
	else
	{		
		SHD_fb = getTag_("1_SCHD_XB03");
		SHD_relay = getTag_("1_SCHD_XG21");
		
		if(SHD_relay.value.dsc && (SHD_fb.value.dsc == 0))
		{
			sendTag_("1_NDV_XC04", 0, STATUS_OK);
			sendTag_("1_PWR_SCHD_XC03", 0, STATUS_OK);
			sendTag_("1_EMK01_XC03", 0, STATUS_OK);
			sendTag_("MIXER_AUTOLEVEL", 0, STATUS_OK);
			sendTag_("SHD_ERROR", 1, STATUS_OK);	
		}	
		else
		{
			sendTag_("SHD_ERROR", 0, STATUS_OK);
		}
	}
	dropFunc_(argRef);
	return NULL;
}
void * pump_pwr_sensefail(void * argRef)
{
	tag_t NDV_relay;
	tag_t SHD_relay;
	relayArgs_t arg;
	arg = *((relayArgs_t*)argRef);
	if(arg.firstRun)
	{
		addTag_("1_NDV_XB01",0,TYPE_DIGITAL,arg.n,  SUBSCRIBE);  // Питание на НДВ подано
		addTag_("1_SCHD_XG21",0,TYPE_DIGITAL,arg.n, SUBSCRIBE);  // Питание на ШД подано

		addTag_("1_NDV_XC04",0,TYPE_DIGITAL,arg.n, NOT_SUBSCRIBE);  // Реле НДВ
		addTag_("1_PWR_SCHD_XC03",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // Реле ШД
		addTag_("MIXER_AUTOLEVEL",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE); //Включение автомата поддержания уровня в смесителе
		addTag_("1_EMK01_XC03",0,TYPE_DIGITAL,arg.n,NOT_SUBSCRIBE);  // Электромагнитный клапан
	}
	else
	{		
		NDV_relay = getTag_("1_NDV_XB01");
		SHD_relay = getTag_("1_SCHD_XG21");
		
		if( (SHD_relay.value.dsc == 0) || (NDV_relay.value.dsc == 0))
		{
			sendTag_("1_NDV_XC04", 0, STATUS_OK);
			sendTag_("1_PWR_SCHD_XC03", 0, STATUS_OK);
			sendTag_("1_EMK01_XC03", 0, STATUS_OK);
			sendTag_("MIXER_AUTOLEVEL", 0, STATUS_OK);
		}	
	}
	dropFunc_(argRef);
	return NULL;
}

void * bust_mix_autolevel(void * argRef)
{
	tag_t tag;
	relayArgs_t arg;
	arg = *((relayArgs_t*)argRef);
	if(arg.firstRun)
	{
		addTag_("MIXER_AUTOLEVEL",0,TYPE_DIGITAL,arg.n,SUBSCRIBE);
	}
	else
	{
		tag = getTag_("MIXER_AUTOLEVEL");
		if(tag.value.dsc == 0)
		{
			if(mix_autolevel_arg != NULL)
			{
				bustFunc_(mix_autolevel_arg);
				mix_autolevel_arg = NULL;
			}
		}
	}
	dropFunc_(argRef);
	return NULL;
}
void init_lib(int libno)
{
	initAction_(libno,"mixer_onoff", RUN_MODAL);
	initAction_(libno,"mixer_control",RUN_THREAD);
	initAction_(libno,"bov_autolevel",RUN_MODAL);
	initAction_(libno,"NDV_overpressure",RUN_MODAL);
	initAction_(libno,"NDV_sensefail", RUN_MODAL);
	initAction_(libno,"SHD_sensefail", RUN_MODAL);
	initAction_(libno,"pump_pwr_sensefail", RUN_MODAL);
	initAction_(libno,"bust_mix_autolevel", RUN_MODAL);
	return;
}


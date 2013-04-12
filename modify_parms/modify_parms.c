#include <stdlib.h>
#include <stdio.h>

typedef struct parm_
{
	char name[60];
	float value;
} parm_t;

int main(int argc, char *argv[]) 
{
	int cmd;
	int i = 0;
	void * buffer = NULL;
	unsigned int * parmCnt_r = NULL;
	unsigned int parmCnt;
	FILE * parmFile;
	parm_t * parms;
	if(argc < 2)
	{ 
		printf("WRONG COMMAND LINE PARMS\n");
		return 0;
	}
	printf("0 - Display\n>0 - Add \n<0 - Remove\n");
	scanf("%d", &cmd);
	if(cmd  > 0)
	{
		parmFile = fopen(argv[1], "r");
		fread(&parmCnt,sizeof(int), 1, parmFile);
		buffer = malloc(sizeof(int) + parmCnt*sizeof(parm_t) + (cmd)*sizeof(parm_t));
		fread(buffer + sizeof(int), sizeof(parm_t), parmCnt, parmFile);
		fclose(parmFile);
		parms = buffer + sizeof(int);
		parmCnt_r = buffer;
		*parmCnt_r = cmd + parmCnt;
		for(i = parmCnt; i < *parmCnt_r;i++)
		{
			printf("Parm name >> \n");
			scanf("%60s", parms[i].name);
			printf("Parm value >> \n");
			scanf("%f", &(parms[i].value));
		}
		parmFile = fopen(argv[1], "w");
		fwrite(buffer, sizeof(int)+ (*parmCnt_r)*sizeof(parm_t), 1, parmFile);
		fclose(parmFile);
	}
	if(cmd  == 0)
	{
		parmFile = fopen(argv[1], "r");
		fread(&parmCnt,sizeof(int), 1, parmFile);
		buffer = malloc(parmCnt*sizeof(parm_t));
		parms = buffer;
		fread(buffer,sizeof(parm_t), parmCnt, parmFile);
		fclose(parmFile);
		//printf("%d\n", parmCnt);
		for(i = 0; i < parmCnt; i++)
		{
			printf("%d) %s = %f\n", i+1, parms[i].name, parms[i].value); 
		}
	}
	if(cmd  < 0)
	{
		parmFile = fopen(argv[1], "r");
		fread(&parmCnt,sizeof(int), 1, parmFile);
		buffer = malloc(parmCnt*sizeof(parm_t));
		parms = buffer;
		fread(buffer,sizeof(parm_t), parmCnt, parmFile);
		fclose(parmFile);
		parmCnt--;
		parmFile = fopen(argv[1], "w");
		fwrite(&parmCnt,sizeof(int),1,parmFile);
		for(i = 0; i < parmCnt + 1; i++)
		{
			if(-i-1 != cmd)
				fwrite(&(parms[i]), sizeof(parm_t), 1, parmFile);
		}
		fclose(parmFile);
	}
	free(buffer);
	return EXIT_SUCCESS;
}

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
	void * buffer;
	unsigned int * parmCnt_r;
	unsigned int parmCnt;
	FILE * parmFile;
	parm_t * parms;
	if(argc < 2)
	{
		printf("WRONG COMMAND LINE PARMS\n");
		return 0;
	}
	scanf("%d", &cmd);
	if(cmd >0)
	{
		buffer = malloc(sizeof(int)+ (cmd-1)*sizeof(parm_t));
		parms = buffer + sizeof(int);
		parmCnt_r = buffer;
		*parmCnt_r = cmd - 1;
		for(i = 0; i < cmd - 1;i++)
		{
			scanf("%60s%f", parms[i].name, &(parms[i].value));
		}
		parmFile = fopen(argv[1], "w");
		fwrite(buffer, sizeof(int)+ (cmd-1)*sizeof(parm_t), 1, parmFile);
		fclose(parmFile);
	}
	else
	{
		parmFile = fopen(argv[1], "r");
		fread(&parmCnt,sizeof(int), 1, parmFile);
		buffer = malloc(parmCnt*sizeof(parm_t));
		parms = buffer;
		fread(buffer,sizeof(parm_t), parmCnt, parmFile);
		fclose(parmFile);
		printf("%d\n", parmCnt);
		for(i = 0; i < parmCnt; i++)
		{
			printf("%s = %f\n", parms[i].name, parms[i].value); 
		}
	}
	free(buffer);
	return EXIT_SUCCESS;
}

#include <pthread.h>

// flags for sv_pureskill_action cvar
#define PS_ACTION_PRIVATE_MSG    1
#define PS_ACTION_CENTER_MSG     2
#define PS_ACTION_PUT_SPEC       4
#define PS_ACTION_KICK           8

void PureSkill_RegisterCvars(void);
void * PureSkill_Main(void *arg);

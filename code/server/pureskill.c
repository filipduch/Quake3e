#include <string.h>
#include <unistd.h>
#include <time.h>

#define JSON_IMPLEMENTATION
#include "../qcommon/json.h"
#undef JSON_IMPLEMENTATION
#include "server.h"
#include "pureskill.h"

cvar_t  *sv_pureskill = NULL;
cvar_t  *sv_pureskill_action = NULL;
cvar_t  *sv_pureskill_debug_log = NULL;

void format_time(char *output){
    time_t rawtime;
    struct tm *timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    sprintf(output, "[%d-%02d-%02d %02d:%02d:%02d]", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
}

void logme(const char *text)
{
    char time[128];
    FILE *fp = NULL;
    memset(time, 0, sizeof(time));
    
    if(!sv_pureskill_debug_log || strlen(sv_pureskill_debug_log->string) == 0)
    {
        return;
    }
    
    fp = fopen(sv_pureskill_debug_log->string, "a+");
    if(!fp)
    {
        return;
    }

    format_time(time);
    fprintf(fp, "%s %s\n", time, text);
    fclose(fp);
}

void PureSkill_RegisterCvars(void)
{
    sv_pureskill = Cvar_Get("sv_pureskill", "1", CVAR_SYSTEMINFO);
    sv_pureskill_action = Cvar_Get("sv_pureskill_action", "1", CVAR_SYSTEMINFO);
    sv_pureskill_debug_log = Cvar_Get("sv_pureskill_debug_log", "", CVAR_SYSTEMINFO);
    logme("Registering cvars...");
}

void * PureSkill_Main(void *arg)
{
    int psLastState = -1;
    char serverQuery[128];
    Com_Memset(serverQuery, 0, sizeof(serverQuery));

    // wait for port so we can prepare url to fetch players from server
    do
    {
        Com_Printf("PureSkill: Waiting for port\n");
        sleep(1);
    } while(Cvar_VariableIntegerValue("net_port") == 0);
    Com_Printf("PureSkill: Port acquired %d\n", Cvar_VariableIntegerValue("net_port"));
    Com_sprintf(serverQuery, sizeof(serverQuery), "wget -qO- http://pureskill.tk/serverinfo/%d", Cvar_VariableIntegerValue("net_port"));

    PureSkill_RegisterCvars();

    size_t counter = 0;
    const size_t actionDelay = 30; // wait this many seconds to perform players check
    for(;;sleep(1), counter++)
    {
        if (!com_sv_running || !com_sv_running->integer || !sv_pureskill || !sv_pureskill_action)
        {
            //Com_Printf("PureSkill: Server is not running.\n");
            continue;
        }

        if(psLastState != sv_pureskill->integer)
        {
            if(!sv_pureskill->integer)
            {
                SV_SendServerCommand(NULL, "chat \"^5PureSkill^7: ^5Anticheat is now ^1OFF\"\n");
            }
            else
            {
                SV_SendServerCommand(NULL, "chat \"^5PureSkill^7: ^5Anticheat is now ^2ON\"\n");
            }
            psLastState = sv_pureskill->integer;
        }

        if(counter % actionDelay != 0 || !sv_pureskill->integer)
        {
            continue;
        }
        counter = 0;

        // download players with PS
        FILE *in = popen(serverQuery, "r");
        if(!in)
        {
            logme("Failed to get players list");
            Com_Printf("PureSkill: Failed to get players list\n");
            continue;
        }

        char json[4096];
        Com_Memset(json, 0, sizeof(json));
        int bytesRead = fread(json, 1, sizeof(json), in);
        pclose(in);
        json[sizeof(json)-1] = 0;

        if(bytesRead == 0)
        {
            logme("Empty json file");
            Com_Printf("PureSkill: Empty file - should not happen\n");
            continue;
        }

        char *jsonEnd = json + strlen(json);

        if (JSON_ValueGetType(json, jsonEnd) != JSONTYPE_ARRAY)
        {
            logme("Inavlid json format - array expected");
            Com_Printf("PureSkill: Invalid json format - array expected\n");
            continue;
        }

        int arraySize = JSON_ArrayGetIndex(json, jsonEnd, NULL, 0);
        
		int i;
        for (i=0; i<sv_maxclients->integer; i++)
        {
            client_t *client = &svs.clients[i];
            if(client->state == CS_ACTIVE && client->netchan.remoteAddress.type != NA_BOT)
            {
                int hasPureSkill = 0;
                int cheater = 0;

				int j;
                const char *guidStr = NULL;
                const char *nameStr = NULL;
                for(j=0; j<arraySize; j++)
                {
                    const char *clientObj = JSON_ArrayGetValue(json, jsonEnd, j);
                    if(clientObj == NULL)
                    {
                       logme("clientObj == NULL");
                       continue;
                    }
                    const char *clientNumStr = JSON_ObjectGetNamedValue(clientObj, jsonEnd, "clientnum");
                    const char *cheaterStr = JSON_ObjectGetNamedValue(clientObj, jsonEnd, "cheater");
                    guidStr = JSON_ObjectGetNamedValue(clientObj, jsonEnd, "guid");
                    nameStr = JSON_ObjectGetNamedValue(clientObj, jsonEnd, "name");
                    int currentClientNum = JSON_ValueGetInt(clientNumStr, jsonEnd);
                    if(currentClientNum == i)
                    {
                        hasPureSkill = 1;
                        cheater = JSON_ValueGetInt(cheaterStr, jsonEnd);
                        break;
                    }
                }

                if(cheater)
                {
					// add some additional logging?
                    SV_SendServerCommand(NULL, "chat \"^5PureSkill^7: ^3Kicking ^7%s ^3(suspected cheating)\"\n", client->name);
                    SV_DropClient(client, "was kicked");
                    continue; // he is gone
                }

                if(!hasPureSkill)
                {
                    int psFlags = sv_pureskill_action->integer;
                    playerState_t *ps = SV_GameClientNum(i);
                    if(ps == NULL)
                    {
                        logme("playerState_t ps == NULL");
                        continue;
                    }
                    int team = ps->persistant[PERS_TEAM];
                    int health = ps->persistant[STAT_HEALTH];

                    if(psFlags & PS_ACTION_KICK)
                    {
                        SV_SendServerCommand(NULL, "chat \"^5PureSkill^7: ^3Dear ^7%s ^3this server uses PureSkill Anticheat. Please download it from PureSkill.tk\"\n", client->name);
                        SV_SendServerCommand(client, "disconnect \"%s\"", "Missing PureSkill Anticheat client - visit PureSkill.tk");
                        continue; // he is gone
                    }

                    if(psFlags & PS_ACTION_PRIVATE_MSG)
                    {
                        SV_SendServerCommand(client, "chat \"^5PureSkill^7: ^3Dear ^7%s ^3this server uses PureSkill Anticheat. Please download it from PureSkill.tk\"\n", client->name);
                    }

                    if(psFlags & PS_ACTION_PUT_SPEC && team != TEAM_SPECTATOR)
                    {
                        Cbuf_AddText(va("forceteam %d spectator\n", i));
                        sleep(1);
                    }

                    if(psFlags & PS_ACTION_CENTER_MSG && (health <= 0 || team == TEAM_SPECTATOR))
                    {
                        SV_SendServerCommand(client, "cp \"Dear ^7%s ^7this server uses PureSkill Anticheat. Please download it from PureSkill.tk\n\"\n", client->name);
                    }
                }
            }
        }
    }

    return NULL;
}

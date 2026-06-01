// game.c — game sync over HTTP (works through ngrok; no UDP).
// Sends my state to the lobby server and reads opponents back, using curl,
// exactly like the chat polling. Same API as before so main.c is unchanged.

#include "game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define GSRV_FILE "desk_server.txt"     // same lobby server URL as everything else

static char g_url[256] = "http://localhost:8080";
static char g_myname[24] = "";

static RemotePlayer g_remote[4];
static int g_remoteCount = 0;
static int g_pendingDamage = 0;

static int run_capture(const char *cmd, char *out, size_t n) {
    out[0] = '\0';
#if defined(_WIN32)
    FILE *fp = _popen(cmd, "r");
#else
    FILE *fp = popen(cmd, "r");
#endif
    if (!fp) return 0;
    size_t total = 0; size_t r;
    while ((r = fread(out+total, 1, n-1-total, fp)) > 0) { total += r; if (total >= n-1) break; }
    out[total] = '\0';
#if defined(_WIN32)
    _pclose(fp);
#else
    pclose(fp);
#endif
    return 1;
}

static void load_url(void){
    FILE *fp=fopen(GSRV_FILE,"r");
    if(!fp) return;
    if(fgets(g_url,sizeof(g_url),fp)){
        size_t n=strlen(g_url);
        while(n>0&&(g_url[n-1]=='\n'||g_url[n-1]=='\r'||g_url[n-1]==' '||g_url[n-1]=='/')) g_url[--n]='\0';
    }
    fclose(fp);
}

int game_init(const char *myName){
    load_url();
    if(myName){ strncpy(g_myname,myName,sizeof(g_myname)-1); g_myname[sizeof(g_myname)-1]='\0'; }
    return 1;
}
void game_shutdown(void){ g_remoteCount=0; g_pendingDamage=0; }

static int find_remote(const char *name){
    for(int i=0;i<g_remoteCount;i++) if(strcmp(g_remote[i].name,name)==0) return i;
    if(g_remoteCount>=4) return -1;
    int i=g_remoteCount++;
    memset(&g_remote[i],0,sizeof(RemotePlayer));
    strncpy(g_remote[i].name,name,sizeof(g_remote[i].name)-1);
    return i;
}

void game_send_state(float x,float y,float z,float yaw,float pitch,int hp,int score){
    char st[160], cmd[512], out[64];
    snprintf(st,sizeof(st),"%.2f|%.2f|%.2f|%.3f|%.3f|%d|%d",x,y,z,yaw,pitch,hp,score);
#if defined(_WIN32)
    snprintf(cmd,sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 3 \"%s/gset?name=%s&st=%s\" >NUL 2>&1", g_url,g_myname,st);
    system(cmd);
#else
    snprintf(cmd,sizeof(cmd),
        "curl -s -m 3 \"%s/gset?name=%s&st=%s\" >/dev/null 2>&1 &", g_url,g_myname,st);
    system(cmd);
#endif
    (void)out;
}

void game_send_hit(const char *targetName,int damage){
    char cmd[512];
#if defined(_WIN32)
    snprintf(cmd,sizeof(cmd),
        "cmd /c start \"\" /b curl -s -m 3 \"%s/ghit?target=%s&dmg=%d\" >NUL 2>&1", g_url,targetName,damage);
#else
    snprintf(cmd,sizeof(cmd),
        "curl -s -m 3 \"%s/ghit?target=%s&dmg=%d\" >/dev/null 2>&1 &", g_url,targetName,damage);
#endif
    system(cmd);
}

void game_poll(void){
    // Fetch all live states.
    char cmd[512], out[2048];
    snprintf(cmd,sizeof(cmd),"curl -s -m 3 \"%s/gget\"", g_url);
    if(run_capture(cmd,out,sizeof(out))){
        char *line = strtok(out,"\n");
        while(line){
            // name|x|y|z|yaw|pitch|hp|score
            char name[24]; float x,y,z,yaw,pitch; int hp,score;
            char *bar=strchr(line,'|');
            if(bar){
                int ln=(int)(bar-line); if(ln>=(int)sizeof(name)) ln=sizeof(name)-1;
                memcpy(name,line,ln); name[ln]='\0';
                if(sscanf(bar+1,"%f|%f|%f|%f|%f|%d|%d",&x,&y,&z,&yaw,&pitch,&hp,&score)==7
                   && strcmp(name,g_myname)!=0){
                    int idx=find_remote(name);
                    if(idx>=0){
                        g_remote[idx].x=x;g_remote[idx].y=y;g_remote[idx].z=z;
                        g_remote[idx].yaw=yaw;g_remote[idx].pitch=pitch;
                        g_remote[idx].hp=hp;g_remote[idx].score=score;g_remote[idx].active=1;
                    }
                }
            }
            line = strtok(NULL,"\n");
        }
    }
    // Fetch damage owed to me.
    snprintf(cmd,sizeof(cmd),"curl -s -m 3 \"%s/gdmg?name=%s\"", g_url,g_myname);
    if(run_capture(cmd,out,sizeof(out))){
        int d=atoi(out);
        if(d>0) g_pendingDamage += d;
    }
}

int game_get_opponent(RemotePlayer *out){
    for(int i=0;i<g_remoteCount;i++) if(g_remote[i].active){ *out=g_remote[i]; return 1; }
    return 0;
}
int game_take_damage(void){ int d=g_pendingDamage; g_pendingDamage=0; return d; }

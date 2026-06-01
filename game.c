// game.c — game sync over HTTP, on a BACKGROUND THREAD so it never stalls the
// render loop. The main thread only touches shared state (guarded by a mutex);
// all the slow curl work happens on the worker thread.

#include "game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(_WIN32)
  #include <windows.h>
  #include <process.h>
#else
  #include <pthread.h>
  #include <unistd.h>
#endif

#define GSRV_FILE "desk_server.txt"

static char g_url[256] = "http://localhost:8080";
static char g_myname[24] = "";

static RemotePlayer g_remote[4];
static int g_remoteCount = 0;
static int g_pendingDamage = 0;

// State the main thread writes for the worker to send:
static volatile int   g_running = 0;
static float g_sx,g_sy,g_sz,g_syaw,g_spitch; static int g_shp, g_sscore;
// outgoing hit queue (simple single-slot is enough at our fire rate)
static char g_hitTarget[24] = ""; static int g_hitDmg = 0;

// ---- cross-platform mutex ----
#if defined(_WIN32)
static CRITICAL_SECTION g_mx;
#define LOCK()   EnterCriticalSection(&g_mx)
#define UNLOCK() LeaveCriticalSection(&g_mx)
#else
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK()   pthread_mutex_lock(&g_mx)
#define UNLOCK() pthread_mutex_unlock(&g_mx)
#endif

static int run_capture(const char *cmd, char *out, size_t n) {
    out[0] = '\0';
#if defined(_WIN32)
    FILE *fp = _popen(cmd, "r");
#else
    FILE *fp = popen(cmd, "r");
#endif
    if (!fp) return 0;
    size_t total = 0, r;
    while ((r = fread(out+total,1,n-1-total,fp)) > 0) { total += r; if (total>=n-1) break; }
    out[total] = '\0';
#if defined(_WIN32)
    _pclose(fp);
#else
    pclose(fp);
#endif
    return 1;
}

static int find_remote(const char *name){
    for(int i=0;i<g_remoteCount;i++) if(strcmp(g_remote[i].name,name)==0) return i;
    if(g_remoteCount>=4) return -1;
    int i=g_remoteCount++;
    memset(&g_remote[i],0,sizeof(RemotePlayer));
    strncpy(g_remote[i].name,name,sizeof(g_remote[i].name)-1);
    return i;
}

// The worker loop: send my state, fetch others, fetch damage — repeat ~12x/sec.
static void worker_body(void){
    char cmd[768], out[2048];
    while (g_running) {
        // snapshot my outgoing state under lock
        float x,y,z,yaw,pitch; int hp,score; char ht[24]; int hd;
        LOCK();
        x=g_sx;y=g_sy;z=g_sz;yaw=g_syaw;pitch=g_spitch;hp=g_shp;score=g_sscore;
        strncpy(ht,g_hitTarget,sizeof(ht)); ht[sizeof(ht)-1]='\0'; hd=g_hitDmg;
        g_hitTarget[0]='\0'; g_hitDmg=0;
        UNLOCK();

        // send my state (blocking is fine — we're off the render thread)
        char st[160];
        snprintf(st,sizeof(st),"%.2f|%.2f|%.2f|%.3f|%.3f|%d|%d",x,y,z,yaw,pitch,hp,score);
        snprintf(cmd,sizeof(cmd),"curl -s -m 3 -H \"ngrok-skip-browser-warning: 1\" \"%s/gset?name=%s&st=%s\"", g_url,g_myname,st);
        run_capture(cmd,out,sizeof(out));

        // send a hit if queued
        if (ht[0] && hd>0) {
            snprintf(cmd,sizeof(cmd),"curl -s -m 3 -H \"ngrok-skip-browser-warning: 1\" \"%s/ghit?target=%s&dmg=%d\"", g_url,ht,hd);
            run_capture(cmd,out,sizeof(out));
        }

        // fetch all states
        snprintf(cmd,sizeof(cmd),"curl -s -m 3 -H \"ngrok-skip-browser-warning: 1\" \"%s/gget\"", g_url);
        if (run_capture(cmd,out,sizeof(out))) {
            LOCK();
            char *line=strtok(out,"\n");
            while(line){
                char name[24]; float rx,ry,rz,ryaw,rpitch; int rhp,rscore;
                char *bar=strchr(line,'|');
                if(bar){
                    int ln=(int)(bar-line); if(ln>=(int)sizeof(name)) ln=sizeof(name)-1;
                    memcpy(name,line,ln); name[ln]='\0';
                    if(sscanf(bar+1,"%f|%f|%f|%f|%f|%d|%d",&rx,&ry,&rz,&ryaw,&rpitch,&rhp,&rscore)==7
                       && strcmp(name,g_myname)!=0){
                        int idx=find_remote(name);
                        if(idx>=0){ g_remote[idx].x=rx;g_remote[idx].y=ry;g_remote[idx].z=rz;
                                    g_remote[idx].yaw=ryaw;g_remote[idx].pitch=rpitch;
                                    g_remote[idx].hp=rhp;g_remote[idx].score=rscore;g_remote[idx].active=1; }
                    }
                }
                line=strtok(NULL,"\n");
            }
            UNLOCK();
        }

        // fetch damage owed to me
        snprintf(cmd,sizeof(cmd),"curl -s -m 3 -H \"ngrok-skip-browser-warning: 1\" \"%s/gdmg?name=%s\"", g_url,g_myname);
        if (run_capture(cmd,out,sizeof(out))) {
            int d=atoi(out);
            if(d>0){ LOCK(); g_pendingDamage+=d; UNLOCK(); }
        }

        // pace the loop ~12x/sec
#if defined(_WIN32)
        Sleep(80);
#else
        usleep(80000);
#endif
    }
}

#if defined(_WIN32)
static unsigned __stdcall worker_thunk(void *arg){ (void)arg; worker_body(); return 0; }
static HANDLE g_thread = NULL;
#else
static void *worker_thunk(void *arg){ (void)arg; worker_body(); return NULL; }
static pthread_t g_thread;
#endif

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
#if defined(_WIN32)
    InitializeCriticalSection(&g_mx);
#endif
    g_shp=100;
    g_running=1;
#if defined(_WIN32)
    g_thread=(HANDLE)_beginthreadex(NULL,0,worker_thunk,NULL,0,NULL);
#else
    pthread_create(&g_thread,NULL,worker_thunk,NULL);
#endif
    return 1;
}

void game_shutdown(void){
    g_running=0;
#if defined(_WIN32)
    if(g_thread){ WaitForSingleObject(g_thread,1500); CloseHandle(g_thread); g_thread=NULL; }
    DeleteCriticalSection(&g_mx);
#else
    pthread_join(g_thread,NULL);
#endif
}

// --- These are now INSTANT: they just update shared state. No curl here. ---
void game_send_state(float x,float y,float z,float yaw,float pitch,int hp,int score){
    LOCK(); g_sx=x;g_sy=y;g_sz=z;g_syaw=yaw;g_spitch=pitch;g_shp=hp;g_sscore=score; UNLOCK();
}
void game_send_hit(const char *targetName,int damage){
    LOCK(); strncpy(g_hitTarget,targetName,sizeof(g_hitTarget)-1); g_hitTarget[sizeof(g_hitTarget)-1]='\0'; g_hitDmg=damage; UNLOCK();
}
void game_poll(void){ /* no-op now: the worker thread does the work */ }

int game_get_opponent(RemotePlayer *out){
    int found=0;
    LOCK();
    for(int i=0;i<g_remoteCount;i++) if(g_remote[i].active){ *out=g_remote[i]; found=1; break; }
    UNLOCK();
    return found;
}
int game_take_damage(void){ int d; LOCK(); d=g_pendingDamage; g_pendingDamage=0; UNLOCK(); return d; }

// voice.h — simple voice API used by main.c.
// Implementation lives in voice.c (which includes miniaudio + sockets and does
// NOT include raylib, to avoid windows.h name clashes).
#ifndef VOICE_H
#define VOICE_H

int  voice_init(const char *myName); // returns 1 on success, 0 on failure
void voice_shutdown(void);
void voice_set_talking(int on);
int  voice_is_talking(void);
void voice_poll(void);        // drain incoming audio; call every frame

// Master output volume, 0..200 (percent). 100 = normal.
void voice_set_master(int volume0to200);

// Per-person controls, keyed by username.
void voice_set_peer_volume(const char *name, int volume0to200);
void voice_set_peer_mute(const char *name, int muted);
int  voice_get_peer_volume(const char *name);   // returns 0..200 (default 100)
int  voice_get_peer_mute(const char *name);     // returns 1 if muted

#endif

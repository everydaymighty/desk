// voice.h — simple voice API used by main.c.
// Implementation lives in voice.c (which includes miniaudio + sockets and does
// NOT include raylib, to avoid windows.h name clashes).
#ifndef VOICE_H
#define VOICE_H

int  voice_init(void);        // returns 1 on success, 0 on failure
void voice_shutdown(void);
void voice_set_talking(int on);
int  voice_is_talking(void);
void voice_poll(void);        // drain incoming audio; call every frame

#endif

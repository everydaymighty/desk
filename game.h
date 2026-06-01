// game.h — networking for the 1v1 arena. Implementation in game.c (no raylib).
#ifndef GAME_H
#define GAME_H

typedef struct {
    char  name[24];
    float x, y, z;        // position
    float yaw, pitch;     // look angles
    int   hp;             // 0..100
    int   score;          // rounds won
    int   active;         // 1 if we've heard from them recently
} RemotePlayer;

int  game_init(const char *myName);   // 1 ok, 0 fail
void game_shutdown(void);

// Send my current state (call ~20x/sec).
void game_send_state(float x, float y, float z, float yaw, float pitch, int hp, int score);
// Report that I hit someone (so their client subtracts health).
void game_send_hit(const char *targetName, int damage);

// Drain incoming packets; updates the remote player table + hit inbox.
void game_poll(void);

// Get the (single) opponent's state. Returns 1 if an opponent is known.
int  game_get_opponent(RemotePlayer *out);

// If someone hit ME since last call, returns total damage and clears it.
int  game_take_damage(void);

#endif

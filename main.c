
#include "game_title.h"

//#link "gamebackground.s"

/*
A platform game with a randomly generated stage.
Uses the Famitone2 library for sound and music.
It scrolls vertically (horizontal mirroring) and nametable
updates are performed offscreen one row at a time with the
draw_floor_line() function.
The enemies use the same movement logic as the player, just
with random inputs.
*/

#include <stdlib.h>
#include <string.h>

// include NESLIB header
#include "neslib.h"

// include CC65 NES Header (PPU)
#include <nes.h>

// BCD arithmetic support
#include "bcd.h"
//#link "bcd.c"

// VRAM update buffer
#include "vrambuf.h"
//#link "vrambuf.c"

// link the pattern table into CHR ROM
//#link "chr_generic.s"

// famitone2 library
//#link "famitone2.s"

extern const byte game_title_pal[16];
extern const byte game_background_rle[];

#define FP_BITS  4
// music and sfx
//#link "music_dangerstreets.s"
extern char danger_streets_music_data[];
//#link "demosounds.s"
extern char demo_sounds[];

// indices of sound effects (0..3)
typedef enum { SND_START, SND_HIT, SND_COIN, SND_JUMP } SFXIndex;

const unsigned char palTitle[16]={ 0x0f,0x07,0x38,0x38,0x0f,0x27,0x29,0x39,0x0f,0x06,0x16,0x26,0x0f,0x09,0x19,0x29 };


///// DEFINES

#define COLS 30		// floor width in tiles
#define ROWS 60		// total scrollable height in tiles

#define MAX_FLOORS 20		// total # of floors in a stage
#define GAPSIZE 4		// gap size in tiles
#define BOTTOM_FLOOR_Y 2	// offset for bottommost floor

#define MAX_ACTORS 8		// max # of moving actors
#define SCREEN_Y_BOTTOM 208	// bottom of screen in pixels
#define ACTOR_MIN_X 16		// leftmost position of actor
#define ACTOR_MAX_X 228		// rightmost position of actor
#define ACTOR_SCROLL_UP_Y 110	// min Y position to scroll up
#define ACTOR_SCROLL_DOWN_Y 140	// max Y position to scroll down
#define JUMP_VELOCITY 18	// Y velocity when jumping

// constants for various tiles
#define CH_BORDER 0x40
#define CH_FLOOR 0xf4
#define CH_LADDER 0xd4
#define CH_ITEM 0xc4
#define CH_BLANK 0x20
#define CH_BASEMENT 0x97

///// GLOBALS
static int i;
static int iy,dy;
static int ct;
static unsigned char wait;
static unsigned char frame_cnt;
static unsigned char bright;
// vertical scroll amount in pixels
static int scroll_pixel_yy = 0;

// vertical scroll amount in tiles (scroll_pixel_yy / 8)
static byte scroll_tile_y = 0;

// last screen Y position of player sprite
static byte player_screen_y = 0;

// score (BCD)
static byte score = 0;

static byte score1 = 0;

static byte score2 = 0;

static byte score3 = 0;

// screen flash animation (virtual bright)
static byte vbright = 4;

// random byte between (a ... b-1)
// use rand() because rand8() has a cycle of 255
byte rndint(byte a, byte b) {
  return (rand() % (b-a)) + a;
}

// return nametable address for tile (x,y)
// assuming vertical scrolling (horiz. mirroring)
word getntaddr(byte x, byte y) {
  word addr;
  if (y < 30) {
    addr = NTADR_A(x,y);	// nametable A
  } else {
    addr = NTADR_C(x,y-30);	// nametable C
  }
  return addr;
}

// convert nametable address to attribute address
word nt2attraddr(word a) {
  return (a & 0x2c00) | 0x3c0 |
    ((a >> 4) & 0x38) | ((a >> 2) & 0x07);
}

/// METASPRITES

// define a 2x2 metasprite
#define DEF_METASPRITE_2x2(name,code,pal)\
const unsigned char name[]={\
        0,      0,      (code)+0,   pal, \
        0,      8,      (code)+1,   pal, \
        8,      0,      (code)+2,   pal, \
        8,      8,      (code)+3,   pal, \
        128};

// define a 2x2 metasprite, flipped horizontally
#define DEF_METASPRITE_2x2_FLIP(name,code,pal)\
const unsigned char name[]={\
        8,      0,      (code)+0,   (pal)|OAM_FLIP_H, \
        8,      8,      (code)+1,   (pal)|OAM_FLIP_H, \
        0,      0,      (code)+2,   (pal)|OAM_FLIP_H, \
        0,      8,      (code)+3,   (pal)|OAM_FLIP_H, \
        128};

// right-facing
DEF_METASPRITE_2x2(playerRStand, 0xd8, 0);
DEF_METASPRITE_2x2(playerRRun1, 0xdc, 0);
DEF_METASPRITE_2x2(playerRRun2, 0xe0, 0);
DEF_METASPRITE_2x2(playerRRun3, 0xe4, 0);
DEF_METASPRITE_2x2(playerRJump, 0xe8, 0);
DEF_METASPRITE_2x2(playerRClimb, 0xec, 0);
DEF_METASPRITE_2x2(playerRSad, 0xf0, 0);

// left-facing
DEF_METASPRITE_2x2_FLIP(playerLStand, 0xd8, 0);
DEF_METASPRITE_2x2_FLIP(playerLRun1, 0xdc, 0);
DEF_METASPRITE_2x2_FLIP(playerLRun2, 0xe0, 0);
DEF_METASPRITE_2x2_FLIP(playerLRun3, 0xe4, 0);
DEF_METASPRITE_2x2_FLIP(playerLJump, 0xe8, 0);
DEF_METASPRITE_2x2_FLIP(playerLClimb, 0xec, 0);
DEF_METASPRITE_2x2_FLIP(playerLSad, 0xf0, 0);

// rescuee at top of building
const unsigned char personToSave[]={
        0,      0,      (0xba)+0,   3, 
        0,      8,      (0xba)+2,   0, 
        8,      0,      (0xba)+1,   3, 
        8,      8,      (0xba)+3,   0, 
        128};

// player run sequence
const unsigned char* const playerRunSeq[16] = {
  playerLRun1, playerLRun2, playerLRun3, 
  playerLRun1, playerLRun2, playerLRun3, 
  playerLRun1, playerLRun2,
  playerRRun1, playerRRun2, playerRRun3, 
  playerRRun1, playerRRun2, playerRRun3, 
  playerRRun1, playerRRun2,
};

///// GAME LOGIC

// struct definition for a single floor
typedef struct Floor {
  byte ypos;		// # of tiles from ground
  int height:4;		// # of tiles to next floor
  int gap:4;		// X position of gap
  int ladder1:4;	// X position of first ladder
  int ladder2:4;	// X position of second ladder
  int objtype:4;	// item type (FloorItem)
  int objpos:4;		// X position of object
} Floor;

// various items the player can pick up
typedef enum FloorItem { ITEM_NONE, ITEM_MINE, ITEM_HEART, ITEM_POWER };

// array of floors
Floor floors[MAX_FLOORS];

// is this x (pixel) position within the gap <gap>?
bool is_in_gap(byte x, byte gap) {
  if (gap) {
    byte x1 = gap*16 + 4;
    return (x > x1 && x < x1+GAPSIZE*8-4);
  } else {
    return false;
  }
}

// is this ladder at (tile) position x within the gap?
bool ladder_in_gap(byte x, byte gap) {
  return gap && x >= gap && x < gap+GAPSIZE*2;
}

// create floors at start of game
void make_floors() {
  byte i;
  
  byte y = BOTTOM_FLOOR_Y;
  Floor* prevlev = &floors[0];
  for (i=0; i<MAX_FLOORS; i++) {
    Floor* lev = &floors[i];
    lev->height = rndint(2,5)*2;
    
    if (i > 0) {
      lev->objtype = rndint(1,4);
      do {
        lev->objpos = rndint(1,14);
      } while (ladder_in_gap(lev->objpos, lev->gap));
    }
    lev->ypos = y;
    y += lev->height;
    prevlev = lev;
  }
  // top floor is special
  floors[MAX_FLOORS-1].height = 15;
  floors[MAX_FLOORS-1].gap = 0;
  floors[MAX_FLOORS-1].ladder1 = 0;
  floors[MAX_FLOORS-1].ladder2 = 0;
  floors[MAX_FLOORS-1].objtype = 0;
  floors[MAX_FLOORS].height =1;
  
  floors[1].gap = 1;
  floors[2].gap = 2;
  floors[3].gap = 6;
  floors[4].gap = 3;
  floors[5].gap = 7;
  floors[6].gap = 2;
  floors[7].gap = 9;
  floors[8].gap = 5;
  floors[9].gap = 11;
  floors[10].gap = 3;
  floors[11].gap = 1;
  floors[12].gap = 9;
  floors[13].gap = 6;
  floors[14].gap = 3;
  floors[15].gap = 1;
  floors[16].gap = 1;
  floors[17].gap = 2;
  floors[18].gap = 6;
  floors[19].gap = 3;
  floors[20].gap = 1;
  
  
  
  floors[0].ladder1=1;
  floors[1].ladder1=2;
  floors[2].ladder1=6;
  floors[3].ladder1=3;
  floors[4].ladder1=7;
  floors[5].ladder1=2;
  floors[6].ladder1=9;
  floors[7].ladder1=5;
  floors[8].ladder1=11;
  floors[9].ladder1=3;
  floors[10].ladder1=1;
  floors[11].ladder1=9;
  floors[12].ladder1=6;
  floors[13].ladder1=3;
  floors[14].ladder1=1;
  floors[15].ladder1=1;
  floors[16].ladder1=2;
  floors[17].ladder1=6;
  floors[18].ladder1=3;
  floors[19].ladder1=1;
  floors[20].ladder1=1;
  
  floors[0].ladder2=2;
  floors[1].ladder2=3;
  floors[2].ladder2=7;
  floors[3].ladder2=4;
  floors[4].ladder2=8;
  floors[5].ladder2=3;
  floors[6].ladder2=10;
  floors[7].ladder2=6;
  floors[8].ladder2=12;
  floors[9].ladder2=4;
  floors[10].ladder2=2;
  floors[11].ladder2=10;
  floors[12].ladder2=7;
  floors[13].ladder2=4;
  floors[14].ladder2=2;
  floors[15].ladder2=2;
  floors[16].ladder2=3;
  floors[17].ladder2=7;
  floors[18].ladder2=4;
  floors[19].ladder2=1;
  floors[20].ladder2=1;
  
  
 
 

 
}


// creete actors on floor_index, if slot is empty
void create_actors_on_floor(byte floor_index);

// draw a nametable line into the frame buffer at <row_height>
// 0 == bottom of stage
void draw_floor_line(byte row_height) {
  char buf[COLS];	// nametable buffer
  char attrs[8];	// attribute buffer 
  byte floor;		// floor counter
  byte dy;		// height in rows above floor
  byte rowy;		// row in nametable (0-59)
  word addr;		// nametable address
  byte i;		// loop counter

  // loop over all floors
  for (floor=0; floor<MAX_FLOORS; floor++) {
    Floor* lev = &floors[floor];
    // compute height in rows above floor
    dy = row_height - lev->ypos;
    // if below bottom floor (in basement)
    if (dy >= 255 - BOTTOM_FLOOR_Y) dy = 0;
    // does this floor intersect the desired row?
    if (dy < lev->height) {
      // first two rows (floor)?
      if (dy <= 1) {
        // iterate through all 32 columns
        for (i=0; i<COLS; i+=2) {
          if (dy) {
            buf[i] = CH_FLOOR+156;		// upper-left
            buf[i+1] = CH_FLOOR+157;// upper-right
          } else {
            buf[i] = CH_FLOOR+172;	// lower-left
            buf[i+1] = CH_FLOOR+173;	// lower-right
          }
        }
        // is there a gap? if so, clear bytes
	if (lev->gap)
          memset(buf+lev->gap*2, 0, GAPSIZE);
      } else {
        // clear buffer
        memset(buf, 0, sizeof(buf));
        // draw walls
        if (floor < MAX_FLOORS-1) {
          buf[0] = CH_FLOOR+44;// left side
          buf[COLS-1] = CH_FLOOR+44;// right side
        }
        // draw ladders
        /*
        if (lev->ladder1) {
          buf[lev->ladder1*2] = CH_LADDER;	// left
          buf[lev->ladder1*2+1] = CH_LADDER+1;	// right
        }
        if (lev->ladder2) {
          buf[lev->ladder2*2] = CH_LADDER;	// left
          buf[lev->ladder2*2+1] = CH_LADDER+1;	// right
        }
        */
        
      }
     
      // draw object, if it exists
      
      if (lev->objtype) {
        byte ch = lev->objtype*4 + CH_ITEM;
        if (dy == 2) {
          buf[lev->objpos*2] = ch+1;	// bottom-left
          buf[lev->objpos*2+1] = ch+3;	// bottom-right
        }
        else if (dy == 3) {
          buf[lev->objpos*2] = ch+0;	// top-left
          buf[lev->objpos*2+1] = ch+2;	// top-right
        }
      }
      
      break;
    }
  }
  // compute row in name buffer and address
  rowy = (ROWS-1) - (row_height % ROWS);
  addr = getntaddr(1, rowy);
  // copy attribute table (every 4th row)
  if ((addr & 0x60) == 0) {
    byte a;
    if (dy==1)
      a = 0x05;	// top of attribute block
    else if (dy==3)
      a = 0x50;	// bottom of attribute block
    else
      a = 0x00;	// does not intersect attr. block
    // write entire row of attribute blocks
    memset(attrs, a, 8);
    vrambuf_put(nt2attraddr(addr), attrs, 8);
  }
  // copy line to screen buffer
  vrambuf_put(addr, buf, COLS);
  // create actors on this floor, if needed
  // TODO: maybe this happens too early?
  if (dy == 0 && (floor >= 2)) {
    create_actors_on_floor(floor);
  }
}
void draw_floor_line2(byte row_height) {
  char buf[COLS];	// nametable buffer
  char attrs[8];	// attribute buffer 
  byte floor;		// floor counter
  byte dy;		// height in rows above floor
  byte rowy;		// row in nametable (0-59)
  word addr;		// nametable address
  byte i;		// loop counter

  // loop over all floors
  for (floor=0; floor<MAX_FLOORS; floor++) {
    Floor* lev = &floors[floor];
    // compute height in rows above floor
    dy = row_height - lev->ypos;
    // if below bottom floor (in basement)
    if (dy >= 255 - BOTTOM_FLOOR_Y) dy = 0;
    // does this floor intersect the desired row?
    if (dy < lev->height) {
      // first two rows (floor)?
      if (dy <= 1) {
        // iterate through all 32 columns
        for (i=0; i<COLS; i+=2) {
          if (dy) {
            buf[i] = CH_FLOOR+152;		// upper-left
            buf[i+1] = CH_FLOOR+152;// upper-right
          } else {
            buf[i] = CH_FLOOR+170;	// lower-left
            buf[i+1] = CH_FLOOR+172;	// lower-right
          }
        }
        // is there a gap? if so, clear bytes
	if (lev->gap)
          memset(buf+lev->gap*2, 0, GAPSIZE);
      } else {
        // clear buffer
        memset(buf, 0, sizeof(buf));
        // draw walls
        if (floor < MAX_FLOORS-1) {
          buf[0] = CH_FLOOR+44;// left side
          buf[COLS-1] = CH_FLOOR+44;// right side
        }
        // draw ladders
        /*
        if (lev->ladder1) {
          buf[lev->ladder1*2] = CH_LADDER;	// left
          buf[lev->ladder1*2+1] = CH_LADDER+1;	// right
        }
        if (lev->ladder2) {
          buf[lev->ladder2*2] = CH_LADDER;	// left
          buf[lev->ladder2*2+1] = CH_LADDER+1;	// right
        }
        */
        
      }
     
      // draw object, if it exists
      
      if (lev->objtype) {
        byte ch = lev->objtype*4 + CH_ITEM;
        if (dy == 2) {
          buf[lev->objpos*2] = ch+1;	// bottom-left
          buf[lev->objpos*2+1] = ch+3;	// bottom-right
        }
        else if (dy == 3) {
          buf[lev->objpos*2] = ch+0;	// top-left
          buf[lev->objpos*2+1] = ch+2;	// top-right
        }
      }
      
      break;
    }
  }
  // compute row in name buffer and address
  rowy = (ROWS-1) - (row_height % ROWS);
  addr = getntaddr(1, rowy);
  // copy attribute table (every 4th row)
  if ((addr & 0x60) == 0) {
    byte a;
    if (dy==1)
      a = 0x05;	// top of attribute block
    else if (dy==3)
      a = 0x50;	// bottom of attribute block
    else
      a = 0x00;	// does not intersect attr. block
    // write entire row of attribute blocks
    memset(attrs, a, 8);
    vrambuf_put(nt2attraddr(addr), attrs, 8);
  }
  // copy line to screen buffer
  vrambuf_put(addr, buf, COLS);
  // create actors on this floor, if needed
  // TODO: maybe this happens too early?
  if (dy == 0 && (floor >= 2)) {
    create_actors_on_floor(floor);
  }
}

// draw entire stage at current scroll position
// filling up entire name table
void draw_entire_stage() {
  byte y;
  for (y=0; y<ROWS; y++) {
    draw_floor_line(y);
    // allow buffer to flush, delaying a frame
    vrambuf_flush();
  }
}
void draw_entire_stage2() {
  byte y;
  for (y=0; y<ROWS; y++) {
    draw_floor_line2(y);
    // allow buffer to flush, delaying a frame
    vrambuf_flush();
  }
}
// get Y pixel position for a given floor
word get_floor_yy(byte floor) {
  return floors[floor].ypos * 8 + 16;
}

// get Y ceiling position for a given floor
word get_ceiling_yy(byte floor) {
  return (floors[floor].ypos + floors[floor].height) * 8 + 16;
}

// set scrolling position
void set_scroll_pixel_yy(int yy) {
  // draw an offscreen line, every 8 pixels
  if ((yy & 7) == 0) {
    // scrolling upward or downward?
    if (yy > scroll_pixel_yy)
      draw_floor_line(scroll_tile_y + 30);	// above
    else
      draw_floor_line(scroll_tile_y - 30);	// below
  }
  // set scroll variables
  scroll_pixel_yy = yy;		// get scroll pos. in pixels
  scroll_tile_y = yy >> 3; 	// divide by 8
  // set scroll registers
  scroll(0, 479 - ((yy + 224) % 480));
}

// redraw a floor when object picked up
void refresh_floor(byte floor) {
  byte y = floors[floor].ypos;	// get floor bottom coordinate
  draw_floor_line(y+2);		// redraw 3rd line
  draw_floor_line(y+3);		// redraw 4th line
}
void refresh_floor2(byte floor) {
  byte y = floors[floor].ypos;	// get floor bottom coordinate
  draw_floor_line2(y+2);		// redraw 3rd line
  draw_floor_line2(y+3);		// redraw 4th line
}

///// ACTORS

typedef enum ActorState {
  INACTIVE, STANDING, WALKING, CLIMBING, JUMPING, FALLING, PACING
};

typedef enum ActorType {
  ACTOR_PLAYER, ACTOR_ENEMY, ACTOR_RESCUE
};

typedef struct Actor {
  word yy;		// Y position in pixels (16 bit)
  byte x;		// X position in pixels (8 bit)
  byte floor;		// floor index
  byte state;		// ActorState
  sbyte yvel;		// Y velocity (when jumping)
  sbyte xvel;		// X velocity (when jumping)
  int name:2;		// ActorType (2 bits)
  int pal:2;		// palette color (2 bits)
  int dir:1;		// direction (0=right, 1=left)
  int onscreen:1;	// is actor onscreen?
} Actor;

Actor actors[MAX_ACTORS];	// all actors

// creete actors on floor_index, if slot is empty
void create_actors_on_floor(byte floor_index) {
  byte actor_index = (floor_index % (MAX_ACTORS-1)) + 1;
  struct Actor* a = &actors[actor_index];
  if (!a->onscreen) {
    Floor *floor = &floors[floor_index];
    a->state = STANDING;
    a->name = ACTOR_ENEMY;
    a->x = rand16();
    a->yy = get_floor_yy(floor_index);
    a->floor = floor_index;
    a->onscreen = 1;
    // rescue person on top of the building
    if (floor_index == MAX_FLOORS-1) {
      a->name = ACTOR_RESCUE;
      a->state = PACING;
      a->x = 0;
      a->pal = 1;
    }
  }
}

void draw_actor(byte i) {
  struct Actor* a = &actors[i];
  bool dir;
  const unsigned char* meta;
  byte x,y; // sprite variables
  // get screen Y position of actor
  int screen_y = SCREEN_Y_BOTTOM - a->yy + scroll_pixel_yy;
  // is it offscreen?
  if (screen_y > 192+8 || screen_y < -18) {
    a->onscreen = 0;
    return; // offscreen vertically
  }
  dir = a->dir;
  switch (a->state) {
    case INACTIVE:
      a->onscreen = 0;
      return; // inactive, offscreen
    case STANDING:
      meta = dir ? playerLStand : playerRStand;
      break;
    case WALKING:
      meta = playerRunSeq[((a->x >> 3) & 1) + (dir?0:8)];
      break;
    case JUMPING:
      meta = dir ? playerLJump : playerRJump;
      break;
    case FALLING:
      meta = dir ? playerLSad : playerRSad;
      break;
    case CLIMBING:
      meta = (a->yy & 4) ? playerLClimb : playerRClimb;
      break;
    case PACING:
      meta = personToSave;
      break;
  }
  // set sprite values, draw sprite
  x = a->x;
  y = screen_y;
  oam_meta_spr_pal(x, y, a->pal, meta);
  // is this actor 0? (player sprite)
  if (i == 0) {
    player_screen_y = y; // save last screen Y position
  }
  a->onscreen = 1; // if we drew the actor, consider it onscreen
  return;
}

// draw the scoreboard, right now just two digits
void draw_scoreboard() {
  if(score3!=1)
  oam_off = oam_spr(24+0, 24, 0x15, 2, oam_off);
  if(score2!=1)
  oam_off = oam_spr(24+8, 24, 0x15, 2, oam_off);
  if(score1!=1)
  oam_off = oam_spr(24+16, 24, 0x15, 2, oam_off);
  if(score!=1)
  oam_off = oam_spr(24+24, 24, 0x15, 2, oam_off);
}

// draw all sprites
void refresh_sprites() {
  byte i;
  // reset sprite index to 0
  oam_off = 0;
  // draw all actors
  for (i=0; i<MAX_ACTORS; i++)
    draw_actor(i);
  // draw scoreboard
  draw_scoreboard();
  // hide rest of actors
  oam_hide_rest(oam_off);
}

// if ladder is close to X position, return ladder X position, otherwise 0
byte is_ladder_close(byte actor_x, byte ladder_pos) {
  byte ladder_x;
  if (ladder_pos == 0)
    return 0;
  ladder_x = ladder_pos * 16;
  return ((byte)(actor_x - ladder_x) < 16) ? ladder_x : 0;
}

// get the closest ladder to the player
byte get_closest_ladder(byte player_x, byte floor_index) {
  Floor* floor = &floors[floor_index];
  byte x;
  if (floor_index >= MAX_FLOORS) return 0;
  x = is_ladder_close(player_x, floor->ladder1);
  if (x) return x;
  x = is_ladder_close(player_x, floor->ladder2);
  if (x) return x;
  return 0;
}

// put the player on the ladder, and move up or down (floor_adjust)
byte mount_ladder(Actor* player, signed char floor_adjust) {
  byte x = get_closest_ladder(player->x, player->floor + floor_adjust);
  if (x) {
    player->x = x + 8;
    player->state = CLIMBING;
    player->floor += floor_adjust;
    return 1;
  } else
    return 0;
}

// should we scroll the screen upward?
void check_scroll_up() {
  if (player_screen_y < ACTOR_SCROLL_UP_Y) {
    set_scroll_pixel_yy(scroll_pixel_yy + 1);	// scroll up
  }
}

// should we scroll the screen downward?
void check_scroll_down() {
  if (player_screen_y > ACTOR_SCROLL_DOWN_Y && scroll_pixel_yy > 0) {
    set_scroll_pixel_yy(scroll_pixel_yy - 1);	// scroll down
  }
}

// actor falls down a floor
void fall_down(struct Actor* actor) {
  actor->floor--;
  actor->state = FALLING;
  actor->xvel = 0;
  actor->yvel = 0;
}

// move an actor (player or enemies)
// joystick - game controller mask
// scroll - if true, we should scroll screen (is player)
void move_actor(struct Actor* actor, byte joystick, bool scroll) {
  switch (actor->state) {
      
    case STANDING:
    case WALKING:
      // left/right has priority over climbing
      if (joystick & PAD_A) {
        actor->state = WALKING;
        actor->xvel = 0;
        actor->yvel = JUMP_VELOCITY;
        if (joystick & PAD_LEFT) actor->xvel = -1;
        if (joystick & PAD_RIGHT) actor->xvel = 1;
        // play sound for player
        if (scroll) sfx_play(SND_JUMP,0);
      } else if (joystick & PAD_LEFT) {
        actor->x--;
        actor->dir = 1;
        actor->state = WALKING;
      } else if (joystick & PAD_RIGHT) {
        actor->x++;
        actor->dir = 0;
        actor->state = WALKING;
      } else if (joystick & PAD_UP) {
        //actor->yy++;
        mount_ladder(actor, 0); // state -> CLIMBING
      } else if (joystick & PAD_DOWN) {
        //actor->yy--;
        mount_ladder(actor, -1); // state -> CLIMBING, floor -= 1
      } else {
        actor->state = STANDING;
      }
      if (scroll) {
        check_scroll_up();
        check_scroll_down();
      }
      break;
      
    case CLIMBING:
      if (joystick & PAD_UP) {
      	if (actor->yy >= get_ceiling_yy(actor->floor)) {
          actor->floor++;
          actor->state = STANDING;
        } else {
          actor->yy++;
        }
      } else if (joystick & PAD_DOWN) {
        if (actor->yy <= get_floor_yy(actor->floor)) {
          actor->state = STANDING;
        } else {
          actor->yy--;
        }
      }
      if (scroll) {
        check_scroll_up();
        check_scroll_down();
      }
      break;
      
    case FALLING:
      if (scroll) {
        check_scroll_up();
        check_scroll_down();
      }
    case JUMPING:
      actor->x += actor->xvel;
      actor->yy += actor->yvel/4;
      actor->yvel -= 1;
      if (actor->yy <= get_floor_yy(actor->floor)) {
	actor->yy = get_floor_yy(actor->floor);
        actor->state = STANDING;
      }
      break;
  }
  // don't allow player to travel past left/right edges of screen
  //if (actor->x > ACTOR_MAX_X) actor->x = ACTOR_MAX_X; // we wrapped around right edge
  //if (actor->x < ACTOR_MIN_X) actor->x = ACTOR_MIN_X;
  // if player lands in a gap, they fall (switch to JUMPING state)
  /*
  if (actor->state <= WALKING && 
      is_in_gap(actor->x, floors[actor->floor].gap)) {
    fall_down(actor);
  }
  */
}
void move_actor2(struct Actor* actor, byte joystick, bool scroll) {

  switch (actor->state) {
      
    case STANDING:
    case WALKING:
      // left/right has priority over climbing
     // left/right has priority over climbing
   
     if (joystick & PAD_LEFT)
        {     
          if(actor->x > ACTOR_MIN_X & ct < 1){
             ct = 0;
             actor->state = WALKING;
             actor->x--; 
             actor->dir = 1; 
          }
          else if (joystick & PAD_RIGHT){
            ct = 1;
            actor->x++;
            actor->dir =0;
          }
          if(actor->x == ACTOR_MAX_X){
            ct = 0;
            actor->x++;
            actor->dir =0;
          }
     }
        
        // play sound for player
        //if (scroll) sfx_play(SND_JUMP,0);
      
    /*
      else if (joystick & PAD_UP) {
        if (actor->yy <= get_ceiling_yy(actor->floor)) {
        
          actor->state = STANDING;
        } else {
           actor->yy++;
      } 
      }else if (joystick & PAD_DOWN) {
         if (actor->yy <= get_floor_yy(actor->floor)) {
          actor->state = STANDING;
        } else {
          actor->yy--;
        }
      } else {
        actor->state = STANDING;
      }
      if (scroll) {
        check_scroll_up();
        check_scroll_down();
      }
     */
      break;
    case CLIMBING:
      if (joystick & PAD_UP) {
      	if (actor->yy >= get_ceiling_yy(actor->floor)) {
          //actor->floor++;
          actor->state = STANDING;
        } else {
          actor->yy++;
        }
      } else if (joystick & PAD_DOWN) {
        if (actor->yy <= get_floor_yy(actor->floor)) {
          actor->state = STANDING;
        } else {
          actor->yy--;
        }
      }
      if (scroll) {
        check_scroll_up();
        check_scroll_down();
      }
      break;
      
    case FALLING:
      if (scroll) {
        check_scroll_up();
        check_scroll_down();
      }
    case JUMPING:
      actor->x += actor->xvel;
      actor->yy += actor->yvel/4;
      actor->yvel -= 1;
      if (actor->yy <= get_floor_yy(actor->floor)) {
	actor->yy = get_floor_yy(actor->floor);
        actor->state = STANDING;
      }
      break;
      
  }
  // don't allow player to travel past left/right edges of screen
  if (actor->x > ACTOR_MAX_X) actor->x = ACTOR_MAX_X; // we wrapped around right edge
  if (actor->x < ACTOR_MIN_X) actor->x = ACTOR_MIN_X;
  // if player lands in a gap, they fall (switch to JUMPING state)
  /*
  if (actor->state <= WALKING && 
      is_in_gap(actor->x, floors[actor->floor].gap)) {
    fall_down(actor);
  }
  */
}

// should we pickup an object? only player does this
void pickup_object(Actor* actor) {
  Floor* floor = &floors[actor->floor];
  byte objtype = floor->objtype;
  // only pick up if there's an object, and if we're walking or standing
  if (objtype && actor->state <= WALKING) {
    byte objx = floor->objpos * 16;
    // is the actor close to the object?
    if (actor->x >= objx && actor->x < objx+16) {
      // clear the item from the floor and redraw
      floor->objtype = 0;
      refresh_floor(actor->floor);
      // did we hit a mine?
      if (objtype == ITEM_MINE) {
        // we hit a mine, fall down
        fall_down(actor);
        sfx_play(SND_HIT,0);
        vbright = 8; // flash
      } else {
        // we picked up an object, add to score
        //score = bcd_add(score, 10);
        sfx_play(SND_COIN,0);
      }
    }
  }
}

// read joystick 0 and move the player
void move_player() {
  byte joy = pad_poll(0);
  move_actor(&actors[0], joy, true);
  pickup_object(&actors[0]);
}
void move_player2() {
  byte joy = pad_poll(0);
  move_actor2(&actors[0], joy, true);
  pickup_object(&actors[0]);
}

// returns absolute value of x
byte iabs(int x) {
  return x >= 0 ? x : -x;
}

// check to see if actor collides with any non-player actor
bool check_collision(Actor* a) {
  byte i;
  byte afloor = a->floor;
  // can't fall through basement
  if (afloor == 0) return false;
  // can't fall if already falling
  if (a->state == FALLING) return false;
  // iterate through entire list of actors
  for (i=1; i<MAX_ACTORS; i++) {
    Actor* b = &actors[i];
    // actors must be on same floor and within 8 pixels
    if (b->onscreen &&
        afloor == b->floor && 
        iabs(a->yy - b->yy) < 8 && 
        iabs(a->x - b->x) < 8) {
        if(score2 == 1)
          {
            score3 = 1;
          }
        if(score1 == 1)
        {
          score2 = 1;
        }
        if(score == 1)
        {
          score1 = 1;
        }
         score = 1;
      return true;
    }
  }
  return false;
}

///

const char* RESCUE_TEXT = 
  "Is this a rescue?\n"
  "I am just hanging out\n"
  "on top of this building.\n"
  "Get lost!!!";

// draw a message on the screen
void type_message(const char* charptr) {
  char ch;
  byte x,y;
  x = 2;
  // compute message y position relative to scroll
  y = ROWS*3 + 39 - scroll_tile_y;
  // repeat until end of string (0) is read
  while ((ch = *charptr++)) {
    while (y >= 60) y -= 60; // compute (y % 60)
    // newline character? go to start of next line
    if (ch == '\n') {
      x = 2;
      y++;
    } else {
      // put character into nametable
      vrambuf_put(getntaddr(x, y), &ch, 1);
      x++;
    }
    // typewriter sound
    sfx_play(SND_HIT,0);
    // flush buffer and wait a few frames
    vrambuf_flush();
    delay(5);
  }
}

// reward scene when player reaches roof
void rescue_scene() {
  // make player face to the left
  actors[0].dir = 1;
  actors[0].state = STANDING;
  refresh_sprites();
  music_stop();
  type_message(RESCUE_TEXT);
  // wait 2 seconds
  delay(100);
}

// game loop
void play_scene() {
  byte i;
  // initialize actors array  
  memset(actors, 0, sizeof(actors));
  actors[0].state = STANDING;
  actors[0].name = ACTOR_PLAYER;
  actors[0].pal = 3;
  actors[0].x = 64;
  actors[0].floor = 0;
  actors[0].yy = get_floor_yy(0);
  // put actor at bottom
  set_scroll_pixel_yy(0);
  // draw initial view of level into nametable
  draw_entire_stage();
  // repeat until player reaches the roof
  while (actors[0].floor != MAX_FLOORS-1) {
    // flush VRAM buffer (waits next frame)
    vrambuf_flush();
    refresh_sprites();
    move_player();
    // move all the actors
    for (i=2; i<MAX_ACTORS; i++) {
      move_actor2(&actors[i], rand8(), false);
    }
    // see if the player hit another actor
    if (check_collision(&actors[0])) {
      fall_down(&actors[0]);
      sfx_play(SND_HIT,0);
      vbright = 8; // flash
    }
    // flash effect
    if (vbright > 4) {
      pal_bright(--vbright);
    }
  }
  // player reached goal; reward scene  
  //rescue_scene();
}
void play_scene2() {
  byte i;
  // initialize actors array  
  memset(actors, 0, sizeof(actors));
  actors[0].state = STANDING;
  actors[0].name = ACTOR_PLAYER;
  actors[0].pal = 3;
  actors[0].x = 64;
  actors[0].floor = 0;
  actors[0].yy = get_floor_yy(0);
  // put actor at bottom
  set_scroll_pixel_yy(0);
  // draw initial view of level into nametable
  draw_entire_stage2();
  // repeat until player reaches the roof
  while (actors[0].floor != MAX_FLOORS-1) {
    // flush VRAM buffer (waits next frame)
    vrambuf_flush();
    refresh_sprites();
    move_player2();
    // move all the actors
    for (i=1; i<MAX_ACTORS; i++) {
      move_actor2(&actors[i], rand16(), false);
    }
    // see if the player hit another actor
    if (check_collision(&actors[0])) {
      fall_down(&actors[0]);
      sfx_play(SND_HIT,0);
      vbright = 8; // flash
    }
    // flash effect
    if (vbright > 4) {
      pal_bright(--vbright);
    }
  }
  // player reached goal; reward scene  
  //rescue_scene();
}

/*{pal:"nes",layout:"nes"}*/
const char PALETTE[32] = { 
  0x0F,			// background color

  0x27,0x17,0x29, 0x00,	// ladders and pickups
  0x17,0x29,0x39, 0x00,	// floor blocks
  0x00,0x10,0x20, 0x00,
  0x06,0x16,0x26, 0x00,

  0x16,0x35,0x24, 0x00,	// enemy sprites
  0x00,0x37,0x25, 0x00,	// rescue person
  0x0D,0x11,0x3A, 0x00,
  0x37,0x27,0x2A	// player sprites
};


void pal_fade_to(unsigned to)
{
  if(!to) {}

  while(bright!=to)
  {
    delay(4);
    if(bright<to) ++bright; else --bright;
    pal_bright(bright);
  }

  if(!bright)
  {
    ppu_off();
    set_vram_update(NULL);
    scroll(0,0);
  }
}
void title_screen(void)
{
  scroll(-8,240);//title is aligned to the color attributes, so shift it a bit to the right

  vram_adr(NAMETABLE_A);
  vram_unrle(game_title);

  vram_adr(NAMETABLE_C);//clear second nametable, as it is visible in the jumping effect
  vram_fill(0,1024);

  pal_bg(palTitle);
  pal_bright(4);
  ppu_on_bg();
  delay(20);//delay just to make it look better

  iy=240<<FP_BITS;
  dy=-8<<FP_BITS;
  frame_cnt=0;
  wait=160;
  bright= 4;

  while(1)
  {
    ppu_wait_frame();

    scroll(0,iy>>FP_BITS);

    if(pad_trigger(0)&PAD_START) break;

    iy+=dy;

    if(iy<0)
    {
      iy=0;
      dy=-dy>>1;
    }

    if(dy>(-8<<FP_BITS)) dy-=2;

    if(wait)
    {
      --wait;
    }
    else
    {
      pal_col(2,(frame_cnt&32)?0x09:0x1B);//blinking press start text
      ++frame_cnt;
    }
  }

  //scroll(-8,0);//if start is pressed, show the title at whole
  //sfx_play(SFX_START,0);

  for(i=0;i<16;++i)//and blink the text faster
  {
    pal_col(2,i&1?0x0f:0x20);
    delay(4);
  }

  pal_fade_to(0);
 
}

// set up PPU
void setup_graphics() {
  ppu_off();
  oam_clear();
  pal_all(PALETTE);
  vram_adr(0x2000);
  vram_fill(CH_BLANK, 0x1000);
  vrambuf_clear();
  set_vram_update(updbuf);
  ppu_on_all();
}

// set up famitone library
void setup_sounds() {
  famitone_init(danger_streets_music_data);
  sfx_init(demo_sounds);
  nmi_set_callback(famitone_update);
}

// main program
void main() {
  setup_sounds();		// init famitone library
  //title_screen();
  while (1) {
    setup_graphics();
    // setup PPU, clear scree
    //sfx_play(SND_START,0);	// play starting sound
    make_floors();		// make random level
    //music_play(0);		// start the music
    play_scene();// play the level
  }
  while (2) {
        setup_graphics();
  	make_floors();
  	play_scene2();
  }
}


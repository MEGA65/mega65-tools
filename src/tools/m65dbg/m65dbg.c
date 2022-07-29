/**
 * m65dbg - An enhanced remote serial debugger/monitor for the mega65 project
 **/

#include <stdio.h>

/* screen_shot.c */
void get_video_state(void);
int do_screen_shot(void);
int do_screen_shot_ascii(void);
void print_screencode(unsigned char c, int upper_case);

int main(int argc, char** argv)
{
  printf("m65dbg would run here.\n");

  return 0;
}

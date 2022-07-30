#ifndef SCREEN_SHOT_H
#define SCREEN_SHOT_H

/*
 * do_screen_shot_ascii()
 *
 * make ascii screenshot and display it on the terminal
 */
int do_screen_shot_ascii(void);

/*
 * do_screen_shot(userfilename)
 *
 * make graphical screenshot and save as PNG in userfilename.
 * If userfilename is NULL, generate filename in local directory.
 */
int do_screen_shot(char *userfilename);

/*
 * get_video_state()
 *
 * get video state form MEGA65 and put it into a bunch of
 * global variables.
 */
void get_video_state(void);

#endif /* SCREEN_SHOT_H */
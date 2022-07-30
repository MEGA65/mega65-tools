#ifndef SCREENSHOT_H
#define SCREENSHOT_H

void get_video_state(void);
int do_screen_shot(char* userfilename);
int do_screen_shot_ascii(void);
void print_screencode(unsigned char c, int upper_case);

#endif // SCREENSHOT_H

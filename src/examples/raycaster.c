/*

  Ported to the MEGA65 by Paul Gardner-Stephen, 2020

Derived from:

Copyright (c) 2004-2019, Lode Vandevenne

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <hal.h>
#include <memory.h>
#include <dirent.h>
#include <fileio.h>


unsigned short i,j;
unsigned char a,b,c,d;

void graphics_mode(void)
{

  // Clear screen RAM first, so that there is no visible glitching
  lfill(0x40000,0x00,0x8000);
  lfill(0x48000,0x00,0x8000);
  lfill(0x50000,0x00,0x8000);
  lfill(0x58000,0x00,0x8000);
  
  // 16-bit text mode, full-colour text for high chars
  POKE(0xD054,0x05);
  // H640, fast CPU
  POKE(0xD031,0xC0);
  // Adjust D016 smooth scrolling for VIC-III H640 offset
  POKE(0xD016,0xC9);
  // 640x200 16bits per char, 16 pixels wide per char
  // = 640/8 x 16 bits = 160 bytes per row
  POKE(0xD058,160);
  POKE(0xD059,160>>8);
  // Draw 80 chars per row
  POKE(0xD05E,80);
  // Put 4000 byte screen at $C000
  POKE(0xD060,0x00);
  POKE(0xD061,0xc0);
  POKE(0xD062,0x00);

  // Layout screen so that graphics data comes from $40000 -- $5FFFF

  i=0x40000/0x40;
  for(a=0;a<80;a++)
    for(b=0;b<25;b++) {
      POKE(0xC000+b*160+a*2+0,i&0xff);
      POKE(0xC000+b*160+a*2+1,i>>8);

      i++;
    }
   
  // Clear colour RAM, 8-bits per pixel
  lfill(0xff80000L,0x00,80*25*2);

  POKE(0xD020,0);
  POKE(0xD021,0);
}

unsigned long pixel_addr;
unsigned char pixel_temp;
void plot_pixel(unsigned long x,unsigned char y,unsigned char colour)
{
  pixel_addr=((x&0xf)>>1)+64L*25L*(x>>4);
  pixel_addr+=y<<3;

  lpoke(0x40000L+pixel_addr,colour);

}




//place the example code below here:

#define screenWidth 640
#define screenHeight 200
#define mapWidth 24
#define mapHeight 24

int worldMap[mapWidth][mapHeight]=
{
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,2,2,2,2,2,0,0,0,0,3,0,3,0,3,0,0,0,1},
  {1,0,0,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,2,0,0,0,2,0,0,0,0,3,0,0,0,3,0,0,0,1},
  {1,0,0,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,2,2,0,2,2,0,0,0,0,3,0,3,0,3,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,4,4,4,4,4,4,4,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,4,0,4,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,4,0,0,0,0,5,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,4,0,4,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,4,0,4,4,4,4,4,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,4,4,4,4,4,4,4,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

//what direction to step in x or y-direction (either +1 or -1)
char stepX;
char stepY;
char hit;
//was a NS or a EW wall hit?
char side;

//length of ray from current position to next x or y-side
unsigned long sideDistX;
unsigned long sideDistY;

unsigned short deltaDistX, deltaDistY;
unsigned long perpWallDist;

short lineHeight,drawStart,drawEnd;

unsigned short mapX, mapY;
unsigned long cameraX;
unsigned long rayDirX;
unsigned long rayDirY;

unsigned char colour;

unsigned short posX = 22*256, posY = 12*256;  //x and y start position
signed short dirX = -1*256, dirY = 0; //initial direction vector
unsigned short planeX = 0;
unsigned short planeY = 169;  // (short)(256*0.66); //the 2d raycaster version of camera plane

int x;

unsigned long temp;

char msg[80+1];

unsigned char char_code;
void print_text(unsigned char x,unsigned char y,unsigned char colour,char *msg)
{
  pixel_addr=0xC000+x*2+y*160;
  while(*msg) {
    char_code=*msg;
    if (*msg>=0xc0&&*msg<=0xe0) char_code=*msg-0x80;
    if (*msg>=0x40&&*msg<=0x60) char_code=*msg-0x40;
    POKE(pixel_addr+0,char_code);
    POKE(pixel_addr+1,0);
    lpoke(0xff80000-0xc000+pixel_addr+0,0x00);
    lpoke(0xff80000-0xc000+pixel_addr+1,colour);
    msg++;
    pixel_addr+=2;
  }
}


unsigned short reciprocal_16(short v)
{
  // Calculate reciprocal as fast as we can
#if 0
  snprintf(msg,80+1,"reciprocal of $%04x(%d) = $%04lx(%ld)\n",
	   v,v,65535/v,65535/v);
  d++; d&=3;
  print_text(0,d,7,msg);
  while(!PEEK(0xD610)) continue;
  POKE(0xD610,0);
#endif
  
  return 65535/v;
}

unsigned short yy;
void vertical_line(unsigned short x,unsigned short start, unsigned short end,unsigned char colour)
{
  plot_pixel(x,0,2);
  for(yy=0;yy<start;yy++) plot_pixel(x,yy,0);
  for(;yy<end;yy++) plot_pixel(x,yy,colour);
  for(;yy<screenHeight;yy++) plot_pixel(x,yy,0);
  plot_pixel(x,0,1);
}

int main(int /*argc*/, char */*argv*/[])
{
  asm ( "sei" );
  
  // Fast CPU, M65 IO
  POKE(0,65);
  POKE(0xD02F,0x47);
  POKE(0xD02F,0x53);

  while(PEEK(0xD610)) POKE(0xD610,0);
  
  POKE(0xD020,0);
  POKE(0xD021,0);

  graphics_mode();
  
  while(1)
  {
    for(x = 0; x < screenWidth; x++)
    {
      //calculate ray position and direction
      cameraX = 256 * 2 * x / screenWidth - 1; //x-coordinate in camera space
      rayDirX = dirX + planeX * cameraX;
      rayDirY = dirY + planeY * cameraX;
      //which box of the map we're in
      mapX = posX;
      mapY = posY;

       //length of ray from one x or y-side to next x or y-side
      //      double deltaDistX = std::abs(1 / rayDirX);
      //      double deltaDistY = std::abs(1 / rayDirY);
      deltaDistX = reciprocal_16(rayDirX);
      deltaDistY = reciprocal_16(rayDirY);

      hit = 0; //was there a wall hit?
      //calculate step and initial sideDist
      if(rayDirX < 0)
      {
        stepX = -1;
        sideDistX = (posX - mapX) * deltaDistX;
	sideDistX = sideDistX >> 16;
      }
      else
      {
        stepX = 1;
        sideDistX = (mapX + 1*256 - posX) * deltaDistX;
	sideDistX = sideDistX >> 16;
      }
      if(rayDirY < 0)
      {
        stepY = -1;
        sideDistY = (posY - mapY) * deltaDistY;
	sideDistY = sideDistY >> 16;
      }
      else
      {
        stepY = 1;
        sideDistY = (mapY + 1*256 - posY) * deltaDistY;
	sideDistY = sideDistY >> 16;
      }
      //perform DDA
      snprintf(msg,80+1,"sideDistX=$%04x, sideDistY=$%04x, stepX=$%x, stepY=$%x.",
	       sideDistX,sideDistY,stepX,stepY);
      d++; d&=3;
      print_text(0,d,7,msg);
      while (hit == 0)
      {
        //jump to next map square, OR in x-direction, OR in y-direction
        if(sideDistX < sideDistY)
        {
          sideDistX += deltaDistX;
          mapX += stepX*256;
          side = 0;
        }
        else
        {
          sideDistY += deltaDistY;
          mapY += stepY*256;
          side = 1;
        }
        //Check if ray has hit a wall
        if(worldMap[mapX>>8][mapY>>8] > 0) hit = 1;
	snprintf(msg,80+1,"mapX=$%04x, mapY=$%04x, stepX=$%02x, stepY=$%02x, hit=%d",
		 mapX,mapY,stepX,stepY,hit);	
	d++; d&=3;
	print_text(0,d,7,msg);
	while(!PEEK(0xD610)) continue;
	POKE(0xD610,0);
      }
      //Calculate distance projected on camera direction (Euclidean distance will give fisheye effect!)
      if(side == 0) perpWallDist = 256*(mapX - posX + (1 - stepX)*128) / rayDirX;
      else          perpWallDist = 256*(mapY - posY + (1 - stepY)*128) / rayDirY;

      //Calculate height of line to draw on screen
      lineHeight = (int)(screenHeight / perpWallDist);

      if (lineHeight<10) lineHeight=10;
      
      //calculate lowest and highest pixel to fill in current stripe
      drawStart = -lineHeight / 2 + screenHeight / 2;
      if(drawStart < 0) drawStart = 0;
      drawEnd = lineHeight / 2 + screenHeight / 2;
      if(drawEnd >= screenHeight) drawEnd = screenHeight - 1;

      //choose wall colour
      colour = worldMap[mapX>>8][mapY>>8];

      //give x and y sides different brightness
      if(side == 1) {colour = colour << 1;}      
      
      //draw the pixels of the stripe as a vertical line
      vertical_line(x, drawStart, drawEnd, colour);
    }

#if 0
    //speed modifiers
    //    double moveSpeed = frameTime * 5.0; //the constant value is in squares/second
    //    double rotSpeed = frameTime * 3.0; //the constant value is in radians/second
    //move forward if no wall in front of you
    if(keyDown(SDLK_UP))
    {
      if(worldMap[(posX + dirX * moveSpeed)>>8][(posY)>>8] == 0) posX += dirX * moveSpeed;
      if(worldMap[(posX)>>8][(posY + dirY * moveSpeed)>>8] == 0) posY += dirY * moveSpeed;
    }
    //move backwards if no wall behind you
    if(keyDown(SDLK_DOWN))
    {
      if(worldMap[(posX - dirX * moveSpeed)>>8][(posY)>>8] == 0) posX -= dirX * moveSpeed;
      if(worldMap[(posX)>>8][(posY - dirY * moveSpeed)>>8] == 0) posY -= dirY * moveSpeed;
    }
    //rotate to the right
    if(keyDown(SDLK_RIGHT))
    {
      //both camera direction and camera plane must be rotated
      double oldDirX = dirX;
      dirX = dirX * cos(-rotSpeed) - dirY * sin(-rotSpeed);
      dirY = oldDirX * sin(-rotSpeed) + dirY * cos(-rotSpeed);
      double oldPlaneX = planeX;
      planeX = planeX * cos(-rotSpeed) - planeY * sin(-rotSpeed);
      planeY = oldPlaneX * sin(-rotSpeed) + planeY * cos(-rotSpeed);
    }
    //rotate to the left
 if(keyDown(SDLK_LEFT))
    {
      //both camera direction and camera plane must be rotated
      double oldDirX = dirX;
      dirX = dirX * cos(rotSpeed) - dirY * sin(rotSpeed);
      dirY = oldDirX * sin(rotSpeed) + dirY * cos(rotSpeed);
      double oldPlaneX = planeX;
      planeX = planeX * cos(rotSpeed) - planeY * sin(rotSpeed);
      planeY = oldPlaneX * sin(rotSpeed) + planeY * cos(rotSpeed);
    }
#endif
  }
}

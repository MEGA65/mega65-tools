/*
  Simple test programme to verify aspects of the function of mega65_ftp.
  In particular, we are having a problem with some sectors (or possibly
  parts of sectors) not being written.

  We do this by pushing a file full of random(ish) data, and then reading
  it back and comparing.

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>

#define FILE_NAME "TEST2.BIN"

#define CONTENT_EQUATION(ID,OFFSET) ((ID+OFFSET+(OFFSET>>8)+(OFFSET>>16))&0xFF)

int create_file(int id,int size)
{
  FILE *f=fopen(FILE_NAME,"wb+");
  if (!f) {
    fprintf(stderr,"ERROR: Failed to write to file '%s'\n",FILE_NAME);
    exit(-1);
  }

  for(int i=0;i<size;i++)
    {
      fputc(CONTENT_EQUATION(id,i),f);
    }

  fclose(f);
  return 0;
}

#define MAX_FILE_SIZE (1024*1024)
unsigned char buff[MAX_FILE_SIZE];

int verify_file(int id,int size)
{
  int errors=0;
  FILE *f=fopen(FILE_NAME,"rb+");
  if (!f) {
    fprintf(stderr,"ERROR: Failed to read from file '%s'\n",FILE_NAME);
    exit(-1);
  }

  bzero(buff,MAX_FILE_SIZE);
  int b=fread(buff,1,MAX_FILE_SIZE,f);
  if (b!=size) {
    fprintf(stderr,"ERROR: File size incorrect: Saw %d, expected %d\n",
	    b,size);
    errors++;
  }
  fclose(f);

  int in_error=0;
  for(int i=0;i<size;i++)
    {
      if (buff[i]!=CONTENT_EQUATION(id,i)) {
	if (in_error) {
	} else {
	  fprintf(stderr,"ERROR: Byte mismatch at offset $%x (saw $%02x, expected $%02x)\n",
		  i,buff[i],CONTENT_EQUATION(id,i));
	  errors++;
	}
	in_error++;
      } else {
	if (in_error>1) fprintf(stderr,"       %d consecutive incorrect bytes.\n",in_error);
	in_error=0;
      }
    }
  if (in_error>1) fprintf(stderr,"       %d consecutive incorrect bytes.\n",in_error);

  return errors;
}

#define FILE_SIZE (256.1*1024)

int main(int argc,char **argv)
{

  char cmd[1024];
  
  if (argc<2) {
    fprintf(stderr,"usage: m65ftp_test <mega65_ftp programme>\n");
    exit(-1);
  }
  
  char *ftp=argv[1];

  for(int i=0;i<256;i++) {
    int size=random()%MAX_FILE_SIZE;
    create_file(i,size);
    snprintf(cmd,1024,"%s -D -c 'put %s'",ftp,FILE_NAME);
    system(cmd);
    unlink(FILE_NAME);
    snprintf(cmd,1024,"%s -D -c 'get %s'",ftp,FILE_NAME);
    system(cmd);
    if (verify_file(i,size)) break;
  }

  return 0;
}

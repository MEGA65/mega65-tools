#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "m65common.h"

#define FINISHED -1

int check_char(char actual_c, char expected_c)
{
  if (expected_c != actual_c)
  {
    printf("ERROR: Expected '%c' char, got '%c' instead!\n", expected_c, actual_c);
    exit(1);
  }
  return 1;
}

int check_char_startflag(char actual_c, char expected_c, int* startflag)
{
  if (!*startflag)
  {
    check_char(actual_c, expected_c);
    *startflag = 1;
    return 1;
  }
  return 0;
}

typedef struct file_info
{
  char title[256];
  char os[256];
  char filename[256];
  char location[256];
  char author[64];
} tfile_info;

typedef struct list_item
{
  void* next;
  void* cur;
} tlist_item;

tlist_item lst_finfos = { 0 };

void assess_key_val(tfile_info *finfo, char* key, char* val)
{
  if (strcmp(key, "title") == 0)
    strcpy(finfo->title, val);
  if (strcmp(key, "os") == 0)
    strcpy(finfo->os, val);
  if (strcmp(key, "filename") == 0)
    strcpy(finfo->filename, val);
  if (strcmp(key, "location") == 0)
    strcpy(finfo->location, val);
  if (strcmp(key, "author") == 0)
    strcpy(finfo->author, val);
}

int read_in_key_or_value_string(tfile_info* finfo, char c, int *iskey, int *quotestart)
{
  static char tmp[4096];
  static int tmplen = 0;
  static char key[4096];
  static char val[4096];

  // read in a key or a value string
  if (c != '"')
  {
    tmp[tmplen] = c;
    tmplen++;
  }
  else
  {
    *quotestart = 0;
    tmp[tmplen] = '\0';
    tmplen = 0;
    if (*iskey)
    {
      strcpy(key, tmp);
      *iskey = 0;
    }
    else
    {
      strcpy(val, tmp);
      *iskey = 1;
      assess_key_val(finfo, key, val);
      return 1;
    }
  }
  return 0;
}

void add_to_list(tfile_info* finfo)
{
  tlist_item* ptr = &lst_finfos;
  while (ptr->cur != NULL)
  {
    if (ptr->next)
    {
      ptr = ptr->next;
    }
    else
    {
      ptr->next = malloc(sizeof(tlist_item));
      ptr = ptr->next;
      memset(ptr, 0, sizeof(tlist_item));
    }
  }

  ptr->cur = malloc(sizeof(tfile_info));
  memcpy(ptr->cur, finfo, sizeof(tfile_info));
}

int read_rows(char* str, int strcnt)
{
  static int squarebrackstart = 0;
  static int curlbrackstart = 0;
  static int quotestart = 0;
  static int valquotestart = 0;
  static tfile_info finfo = { 0 };

  static int iskey = 1;

  for (int k = 0; k < strcnt; k++)
  {
    char c = str[k];

    // skip header bytes and wait for '['
    if (!squarebrackstart)
    {
      if (c != '[')
        continue;
      
      squarebrackstart = 1;
      continue;
    }

    if (!curlbrackstart)
    {
      // skip any ',' preceding '{'
      if (c == ',')
      continue;

      // found the ']' char? (no more '{' expected)
      if (c == ']')
      {
        squarebrackstart = 0;
        return 0;
      }
    }

    if (check_char_startflag(c, '{', &curlbrackstart))
    {
      memset(&finfo, 0, sizeof(tfile_info));
      continue;
    }

    if (!quotestart)
    {
      if (!iskey) // are we expecting a value next?
      {
        if (c == ':')
          continue;
      }
      else
      {
        // are we expecting another
        if (c == ',')
          continue;

        // are we expecting an end of curly-bracket?
        if (c == '}')
        {
          if (finfo.author[0] != '\0' && strcmp(finfo.os, " MEGA65") == 0)
          {
            add_to_list(&finfo);
            memset(&finfo, 0, sizeof(tfile_info));
          }
          curlbrackstart = 0;
          continue;
        }
      }
    }

    if (check_char_startflag(c, '"', &quotestart))
      continue;

    if (read_in_key_or_value_string(&finfo, c, &iskey, &quotestart))
    {
    }
  }
  return 1;
}

void print_items(void)
{
  tlist_item *ptr = &lst_finfos;
  while (ptr != NULL)
  {
    tfile_info *pfinfo = (tfile_info*)ptr->cur;
    printf("title: %s\nfilename: %s\nlocation: %s\nauthor: %s\n\n",
        pfinfo->title, pfinfo->filename, pfinfo->location, pfinfo->author);

    ptr = ptr->next;
  }
}

void read_filehost_struct(void)
{
  char str[4096];
  PORT_TYPE fd = open_tcp_port("tcp#files.mega65.org:80");
  do_write(fd, "GET /php/readfilespublic.php HTTP/1.1\r\n");
  do_write(fd, "Host: files.mega65.org\r\n\r\n");

  do_usleep(100000);

  int count = 0, total = 0;
  while( (count = do_read(fd, str, 4096)) != 0 || total == 0)
  {
    if (!read_rows(str, count))
      break;
  }

  print_items();
}

void main(void)
{
  read_filehost_struct();
}

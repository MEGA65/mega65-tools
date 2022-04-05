#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "m65common.h"

#define FINISHED -1

static int sort_date_flag = 0;

int check_char(char actual_c, char expected_c)
{
  if (expected_c != actual_c) {
    printf("ERROR: Expected '%c' char, got '%c' instead!\n", expected_c, actual_c);
    exit(1);
  }
  return 1;
}

int check_char_startflag(char actual_c, char expected_c, int* startflag)
{
  if (!*startflag) {
    check_char(actual_c, expected_c);
    *startflag = 1;
    return 1;
  }
  return 0;
}

typedef struct file_info {
  char title[256];
  char os[256];
  char filename[256];
  char location[256];
  char author[64];
  char published[64];
} tfile_info;

typedef struct list_item {
  struct list_item* prev;
  struct list_item* next;
  void* cur;
} tlist_item;

tlist_item *lst_finfos = NULL;

void clean_copy(char* dest, char* src)
{
  char* sptr = src;
  char* dptr = dest;

  while (*sptr != '\0') {
    if (*sptr == '\\') {
      sptr++;
      continue;
    }

    *dptr = *sptr;
    sptr++;
    dptr++;
  }
  *dptr = '\0';
}

void cleanup(char *str)
{
  while (*str != 0)
  {
    if (*str == '\\') {
      char *s = str;
      while (*s != '\0')
      {
        *s = *(s+1);
        s++;
      }
    }
    str++;
    if (*str == '\0')
      break;
  }
}

#define DEBUG

void assess_key_val(tfile_info* finfo, char* key, char* val)
{
#ifdef DEBUG
  printf("key: %s, val: %s\n", key, val);
#endif

  if (strcmp(key, "title") == 0)
    strcpy(finfo->title, val);
  if (strcmp(key, "os") == 0)
    strcpy(finfo->os, val);
  if (strcmp(key, "filename") == 0)
    strcpy(finfo->filename, val);
  if (strcmp(key, "location") == 0)
    clean_copy(finfo->location, val);
  if (strcmp(key, "author") == 0)
    strcpy(finfo->author, val);
  if (strcmp(key, "published") == 0) {
    strcpy(finfo->published, val);
    cleanup(finfo->published);
  }
}

int read_in_key_or_value_string(tfile_info* finfo, char prevc, char c, int* iskey, int* quotestart)
{
  static char tmp[4096];
  static int tmplen = 0;
  static char key[4096];
  static char val[4096];

  // read in a key or a value string
  if (c != '"' || (c == '"' && prevc == '\\')) {
    tmp[tmplen] = c;
    tmplen++;
  }
  else {
    *quotestart = 0;
    tmp[tmplen] = '\0';
    tmplen = 0;
    if (*iskey) {
      strcpy(key, tmp);
      *iskey = 0;
    }
    else {
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
  // make first item in list?
  if (lst_finfos == NULL)
  {
    lst_finfos = malloc(sizeof(tlist_item));
    memset(lst_finfos, 0, sizeof(tlist_item));
  }

  tlist_item* itm = lst_finfos;

  // scan to end of list
  while (itm->cur != NULL) {
    if (sort_date_flag) {
      // compare date of currently iterated item against new item
      if (strcmp(finfo->published, ((tfile_info*)itm->cur)->published) <= 0) {
        tlist_item* newitem = malloc(sizeof(tlist_item));

        newitem->next = itm;
        newitem->prev = itm->prev;

        if (itm->prev)
          ((tlist_item*)itm->prev)->next = newitem;
        else
          lst_finfos = newitem; //  push into the first item in the list

        itm->prev = newitem;

        itm = newitem;
        break;
      }
    }
    if (itm->next) {
      itm = itm->next;
    }
    else {
      tlist_item* newitem = malloc(sizeof(tlist_item));
      memset(newitem, 0, sizeof(tlist_item));
      newitem->prev = itm;
      itm->next = newitem;
      itm = itm->next;
      break;
    }
  }

  itm->cur = malloc(sizeof(tfile_info));
  memcpy(itm->cur, finfo, sizeof(tfile_info));
}

int read_rows(char* str, int strcnt)
{
  static int squarebrackstart = 0;
  static int curlbrackstart = 0;
  static int quotestart = 0;
  static tfile_info finfo = { 0 };

  static int iskey = 1;
  char prevc = 0;

  for (int k = 0; k < strcnt; k++) {
    char c = str[k];

    // skip header bytes and wait for '['
    if (!squarebrackstart) {
      if (c != '[')
        continue;

      squarebrackstart = 1;
      continue;
    }

    if (!curlbrackstart) {
      // skip any ',' preceding '{'
      if (c == ',')
        continue;

      // found the ']' char? (no more '{' expected)
      if (c == ']') {
        squarebrackstart = 0;
        return 0;
      }
    }

    if (check_char_startflag(c, '{', &curlbrackstart)) {
      memset(&finfo, 0, sizeof(tfile_info));
      continue;
    }

    if (!quotestart) {
      if (!iskey) // are we expecting a value next?
      {
        if (c == ':')
          continue;
      }
      else {
        // are we expecting another
        if (c == ',')
          continue;

        // are we expecting an end of curly-bracket?
        if (c == '}') {
          if (finfo.author[0] != '\0' && strstr(finfo.os, "MEGA65") != NULL) {
            add_to_list(&finfo);
            memset(&finfo, 0, sizeof(tfile_info));
          }
          curlbrackstart = 0;
          continue;
        }
      }
    }

    if (c == '"' && str[k-1] != '\\' && check_char_startflag(c, '"', &quotestart))
      continue;

    if (read_in_key_or_value_string(&finfo, prevc, c, &iskey, &quotestart)) { }

    prevc = c;
  }
  return 1;
}

// borrow this from mega65_ftp.c for now...
int is_match(char* line, char* pattern, int case_sensitive);

void print_items(char* searchterm)
{
  int cnt = 1;
  tlist_item* itm = lst_finfos;
  while (itm != NULL) {
    tfile_info* pfinfo = (tfile_info*)itm->cur;
    if (!searchterm || (searchterm && strlen(searchterm) == 0) || is_match(pfinfo->title, searchterm, 0) || is_match(pfinfo->filename, searchterm, 0)
        || is_match(pfinfo->author, searchterm, 0)) {
      printf("%d: %s - \"%s\" - author: %s - published: %s\n", cnt, pfinfo->title, pfinfo->filename, pfinfo->author, pfinfo->published);
    }

    itm = (tlist_item*)itm->next;
    cnt++;
  }
}

char cookies[256] = "";

void log_in_and_get_cookie(char* username, char* password)
{
  char str[4096];
  char data[4096];
  sprintf(data, "logindata%%5Busername%%5D=%s&logindata%%5Bpassword%%5D=%s", username, password);
  PORT_TYPE fd = open_tcp_port("tcp#files.mega65.org:80");
  do_write(fd, "POST /php/login.php HTTP/1.1\r\n");
  do_write(fd, "Host: files.mega65.org\r\n");
  do_write(fd, "Accept: */*\r\n");
  do_write(fd, "Content-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n");
  do_write(fd, "X-Requested-With: XMLHttpRequest\r\n");
  sprintf(str, "Content-Length: %d\r\n\r\n", (int)strlen(data));
  do_write(fd, str);
  do_write(fd, data);

  int count = 0, total = 0;
  data[0] = 0;
  while ((count = do_read(fd, str, 4096)) != 0 || total == 0) {
    if (count == 0)
      continue;

    strcat(data, str);
    total += count;
  }

  char* ptr = strtok(data, "\n");
  cookies[0] = 0;
  int ckptr = 0;
  char* cookie_field = "Set-Cookie:";
  do {
    if (strncmp(ptr, cookie_field, strlen(cookie_field)) == 0) {
      ptr += strlen(cookie_field);
      int k = 0;
      do {
        cookies[ckptr] = ptr[k];
        k++;
        ckptr++;
      } while (ptr[k] != ';');
      cookies[ckptr] = ';';
      ckptr++;
      cookies[ckptr] = '\0';
    }
    ptr = strtok(NULL, "\n");
  } while (ptr != NULL);

  close_tcp_port(fd);
}

void clear_list(void)
{
  tlist_item* itm = lst_finfos;
  while (itm != NULL) {
    if (itm->cur)
      free(itm->cur); // delete the file-info

    tlist_item* tmp = (tlist_item*)itm->next;

    free(itm);

    itm = tmp;
  }

  lst_finfos = NULL;
}

void read_filehost_struct(char* searchterm)
{
  char str[4096];

  if (searchterm && strcmp(searchterm, "-t") == 0) {
    sort_date_flag = 1;
    searchterm[0] = '\0';
  }
  else
    sort_date_flag = 0;

  clear_list();
  PORT_TYPE fd = open_tcp_port("tcp#files.mega65.org:80");
  do_write(fd, "GET /php/readfilespublic.php HTTP/1.1\r\n");
  do_write(fd, "Host: files.mega65.org\r\n");
  if (cookies[0] != '\0') {
    cookies[strlen(cookies) - 1] = '\0'; // remove last ';'
    sprintf(str, "Cookie:%s\r\n", cookies);
    do_write(fd, str);
  }
  do_write(fd, "\r\n");

  do_usleep(100000);

  int count = 0, total = 0;
  while ((count = do_read(fd, str, 4096)) != 0 || total == 0) {
    if (!read_rows(str, count))
      break;
  }

  print_items(searchterm);

  close_tcp_port(fd);
}

int check_file_start(char c)
{
  static char* term = "\r\n\r\n";
  static int termidx = 0;

  if (c == term[termidx]) {
    termidx++;
    if (term[termidx] == '\0') {
      termidx = 0;
      return 1;
    }
  }
  else
    termidx = 0;

  return 0;
}

void check_content_length(char c, int* content_length)
{
  static char* term = "Content-Length: ";
  static int termmatch = 0;
  static int termidx = 0;
  static char value[128] = "";
  static int valueidx = 0;

  if (!termmatch) {
    if (c == term[termidx]) {
      termidx++;
      if (term[termidx] == '\0') {
        termmatch = 1;
        termidx = 0;
      }
    }
    else
      termidx = 0;
  }
  // checking for value now
  else {
    if (c == '\r' || c == '\n') {
      // We reached the end of the value, now parse it
      value[valueidx] = '\0';
      *content_length = atoi(value);
      // reset vars for next time
      termmatch = 0;
      termidx = 0;
      valueidx = 0;
    }
    else {
      value[valueidx] = c;
      valueidx++;
    }
  }
}

void strupper(char* dest, char* src)
{
  char* psrc = src;
  char* pdest = dest;
  while (*psrc != '\0') {
    *pdest = toupper(*psrc);
    psrc++;
    pdest++;
  }
  *pdest = '\0';
}

char* download_file_from_filehost(int fileidx)
{
  char* path;
  static char fname[256];
  char* retfname = NULL;

  fname[0] = '\0';

  tlist_item* ptr = lst_finfos;
  int cnt = 1;
  while (ptr != NULL) {
    if (fileidx == cnt)
      break;
    ptr = (tlist_item*)ptr->next;
    cnt++;
  }

  if (fileidx != cnt) {
    printf("ERROR: Invalid file index\n");
    return NULL;
  }

  tfile_info* pfi = (tfile_info*)ptr->cur;

  path = pfi->location;
  strupper(fname, pfi->filename);

  printf("Downloading \"%s\"...\n", fname);

  char str[4096];
  PORT_TYPE fd = open_tcp_port("tcp#files.mega65.org:80");
  sprintf(str, "GET /php/%s HTTP/1.1\r\n", path);
  do_write(fd, str);
  do_write(fd, "Host: files.mega65.org\r\n\r\n");

  do_usleep(100000);

  int count = 0, total = 0;
  int content_length = 0;
  int file_started = 0;
  FILE* f = NULL;
  while (content_length == 0 || total != content_length) {
    count = do_read(fd, str, 4096);
    if (count == 0) {
      do_usleep(10000);
      continue;
    }

    // printf("reading %d bytes...\n", count);
    for (int k = 0; k < count; k++) {
      char c = str[k];
      if (!content_length) {
        check_content_length(c, &content_length);
        if (!content_length)
          continue;
        printf("content_length=%d\n", content_length);
      }

      if (!file_started) {
        // wait for header to finish
        if (check_file_start(c)) {
          file_started = 1;
          f = fopen(fname, "wb");
        }
      }
      else {
        // now start saving out bytes
        fputc(c, f);
        total++;
        if (total == content_length) {
          printf("Download of \"%s\" file complete\n", fname);
          fclose(f);
          retfname = fname;
          break;
        }
      }
    }
    if (content_length != 0 && total == content_length) {
      break;
    }
  }

  close_tcp_port(fd);
  return retfname;
}

/*
void main(void)
{
  read_filehost_struct();
  download_file_from_filehost(25);
}
*/

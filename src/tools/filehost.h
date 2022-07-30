#ifndef FILEHOST_H
#define FILEHOST_H

void log_in_and_get_cookie(char *username, char *password);
void read_filehost_struct(char *searchterm);
char *download_file_from_filehost(int fileidx);

#endif // FILEHOST_H

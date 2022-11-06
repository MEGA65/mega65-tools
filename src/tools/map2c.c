#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
  FILE *in = fopen(argv[1], "rb");
  char *prefix = argv[2];
  FILE *out = fopen(argv[3], "w");

  static const int maxLen = 1023;
  char line[maxLen + 1];
  line[maxLen] = '\0';
  char* address;
  char* label;

  while (fgets(line, maxLen, in) ) {
    address = 0;
    label = 0;
    for (int i = 1; i < maxLen - 3; i++) {
      if (line[i] == ' ') {
        line[i] = '\0';
        address = &line[1];
        label = &line[i + 3];
        break;
      }
    }
    if (address && label) {
      char* tmp = label;
      while (*tmp != '\0') {
        if (*tmp == ' ') {
          *tmp = '\0';
          break;
        }
        tmp++;
      }
      if (strcmp(label, "*")) {
        fprintf(out, "static const int %s_%s=0x%s;\n", prefix, label, address);
      }
    }
  }

  fclose(in);
  fclose(out);
  return 0;
}

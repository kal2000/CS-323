#include <stdio.h>
#include <stdlib.h>

int main()
{
  FILE *file = fopen("./files/specialchars.in","w");
  for(int i=0;i<256;i++) fprintf(file, "%c%c", i, i);
  for(int i=0;i<256;i++) fprintf(file, "%c", i);
  fclose(file);
  file = fopen("./files/specialchars2.in", "w");
  for(int i=0;i<1000;i++) fprintf(file, "%c", 0);
  fclose(file);
  file = fopen("./files/specialchars3.in", "w");
  for(int i=0;i<2000;i++) fprintf(file, "%c", 255);
  fclose(file);
  file = fopen("./files/specialchars4.in", "w");
  for(int i=0;i<2000;i++) fprintf(file, "d");
  fclose(file);
  return 0;
}

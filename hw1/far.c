/*
  Far - mini file archiver
    Far uses a single "archive" file to maintain a collection of regular files
    and directories called "members".  The KEY argument specifies the action
    to be taken (see below); the ARCHIVE argument specifies the name of the
    archive file; and the NAME arguments (if any) are names specifying which
    files or directories to save or restore or delete.
      --Taken from CS323 spec
  Written by Kevin Lai 9/12/12
*/

#define _GNU_SOURCE
#include <linux/limits.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FARFAIL(format,value) fprintf(stderr,format,value), exit(EXIT_FAILURE)
#define STDPERM (0777)
#define MAXLEN (PATH_MAX+2)

//node for stack or linked list
//name hold a strings of a filename, *next is a pointer to another node
typedef struct node_t 
{
  char name[MAXLEN];
  struct node_t* next;
} node;

//stack struct
//contains a pointer to the head node and an int containing the stack size
typedef struct stack_t
{
  node *head;
  int size;
} stack;

//initializes a new stack
void stackInit(stack *s)
{
  s->size = 0;
  s->head = NULL;
}

//mallocs a node with the string nodeName and pushes it to stack s
void stackPush(stack *s, const char *nodeName)
{
  node *n = malloc(sizeof(node));
  strcpy(n->name, nodeName);
  if(s->size == 0) n->next = NULL;
  else n->next = s->head;
  s->head = n;
  s->size++;
}

//frees all nodes in the stack, leaving the stack empty
void freeStack(stack *s)
{
  node *temp = s->head;
  while(temp!=NULL)
  {
    s->head = s->head->next;
    free(temp);
    temp = s->head;
  }
  s->size = 0;
}

//removes all instances of a name from a stack
//takes as parameters the name and the stack
void removeFromStack(stack *s, const char *name)
{
  if(s==NULL) return;
  node *last = NULL;
  node *temp = s->head;
  while(temp != NULL)
  {
    if(strcmp(name, temp->name) == 0)
    {
      if(last == NULL)
      {
        s->head = temp->next;
        free(temp);
        s->size--;
        temp = s->head;
      }
      else
      {
        last->next = temp->next;
        free(temp);
        s->size--;
        temp = last->next;
      }
    }
    else
    {
      last = temp;
      temp = temp->next;
    }
  }
}

//turns trailing slashes in a string to null characters
void removeTrailingSlashes(char* name)
{
  int nameLen = strlen(name);
  if(nameLen == 1) return;
  nameLen--;
  while(name[nameLen] == '/')
  {
    name[nameLen--] = '\0';
    if(nameLen == 1) return;
  }
}

//checks if string name is a path prefix of string prefix
bool checkPrefix(const char *name, const char *prefix)
{
  int nameLen = strlen(name);
  int prefixLen = strlen(prefix);
  if(nameLen <= prefixLen) return false;
  if( strncmp(name, prefix, prefixLen) != 0) return false;

  return (name[prefixLen] == '/')? true : false;
}

//determines if any members of a stack are prefixes of the string "name"
//returns a pointer to the prefix as well
bool stackHasPrefix(stack *s, const char *name, char *prefix)
{
  if (s==NULL) return false;
  node* temp = s->head;
  while(temp != NULL)
  {
    if(checkPrefix(name, temp->name))
    {
      strcpy(prefix, temp->name);
      return true;
    }
    temp = temp->next;
  }
  return false;
}

//check if name is a prefix of members of the stack
bool prefixOfStack(stack *s, const char *name)
{
  if (s==NULL) return false;
  node* temp = s->head;
  while(temp != NULL)
  {
    if(checkPrefix(temp->name, name)) return true;
    temp = temp->next;
  }
  return false;
}

//check if a name is found in a stack. trailing slashes are ignored
bool isNameInStack(stack *s, const char* name)
{
  if(s == NULL) return false;
  node *temp = s->head;
  while(temp!=NULL)
  {
    int nameLen = strlen(temp->name) + 1;
    char temp2[nameLen];
    strcpy(temp2, temp->name);
    removeTrailingSlashes(temp2);
    if( strcmp(name, temp2) == 0) return true;
    temp = temp->next;
  }
  return false;
}

//prints the name contained in each node in the stack. used for debugging
void printStack(stack *s)
{
  printf("Printing stack...\n");
  node *temp = s->head;
  while(temp!=NULL)
  {
    printf("  %s\n", temp->name);
    temp = temp->next;
  }
  printf("Done printing stack.\n");
}

//free a stack and prints a message indicating the archive is corrupted
void archiveCorrupted(stack *s)
{
  fprintf(stderr, "Archive corrupted. Shutting down.\n");
  if(s!=NULL)
  {
    freeStack(s);
    free(s);
  }
}

//creates a temporary archive file if the mode is replace or delete
//takes as parameters the name of the new archive, the name of the existing
//archive, and the mode
void createTemporaryArchiveFileIfNecessary(char *newArchiveName,
  const char* archiveName, char mode)
{
  strcpy(newArchiveName, archiveName);
  strcat(newArchiveName, ".bak");

  if(mode == 'r' || mode == 'd')
  {
    FILE* newArchive = fopen(newArchiveName,"w");
    fclose(newArchive);
  }
}

//takes input from a file and appends it to a far archive in the proper format
//recursively adds files and directories to the archive as well
//takes as parameters the name of the initial file, the name of the archive,
//the stack of found names, and a boolean indicating whether or not a file was
//successfully written
void fileToArchive(const char* fileName, const char* originalName,
  const char* archiveName, stack *found, stack *nameStack, bool *wroteFile)
{
  int c;
  FILE *archive;

  struct stat buf;
  if(lstat(fileName, &buf) != 0)
  {
    if(isNameInStack(nameStack, fileName))
    {
      fprintf(stderr,
        "Lstat failed for %s when trying to archive\n", fileName);
      removeFromStack(nameStack, fileName);
    }
  }
  else if (isNameInStack(found, fileName)) return;
  else if (S_ISDIR(buf.st_mode))
  {
    DIR *dir = opendir(fileName);
    if(dir == NULL)
      fprintf(stderr,"Failed to open directory %s\n", fileName);
    else //recurse into directory
    {
      archive = fopen(archiveName,"a");
      fputs(fileName, archive);
      fprintf(archive,"/\n0|");
      fclose(archive);

      stackPush(found, fileName);
      if(isNameInStack(found, originalName)) *wroteFile = true;

      struct dirent *tempptr;
      while((tempptr = readdir(dir)))
      {
        char tempName[strlen(tempptr->d_name)+strlen(fileName)+1];
        strcpy(tempName, fileName);
        strcat(tempName, "/");
        strcat(tempName, tempptr->d_name);
                
        if((strcmp(tempptr->d_name, ".") != 0) &&
           (strcmp(tempptr->d_name, "..") != 0))
        {
          stackPush(nameStack, tempName);
          fileToArchive(tempName, originalName,
            archiveName, found, nameStack, wroteFile);
        }
      }
      closedir(dir);
    }
  }
  else if(S_ISREG(buf.st_mode))
  {
    FILE *file = fopen(fileName,"r");
    if(file == NULL)
    {
      fprintf(stderr,"Could not open file %s\n", fileName);
      stackPush(found, fileName);
    }
    else
    {
      archive = fopen(archiveName,"a");
      fputs(fileName, archive);
      putc('\n',archive);

      fprintf(archive,"%lld|",(long long)buf.st_size);

      while( (c = getc(file)) != EOF) putc(c, archive);
      *wroteFile = true;
  
      fclose(file);
      fclose(archive);
      stackPush(found, fileName);
    }
  }
}

//Returns the strings before and after the first slash of an input string
//if the first slash is the first character, parse around the second slash
//Return 0 if found a non-first-char slash and -1 if did not find one
//takes as an arguments the input string, and on exit stores the before
//and after strings in the pointers provided when the function is called
int parseFirstSlash(char* input, char* before, char* after)
{
  char *pchr = strchr(input, '/');
  if(pchr == input) //first character is a slash
  {
    pchr = strchr(input+1,'/');
  }
  if(pchr == NULL)
  {
    before = after = NULL;
    return -1;
  }
  else
  {
    strncpy(before, input, (pchr - input));
    before[pchr-input] = '\0';
    strcpy(after, pchr+1);
    return 0;
  }
}

//recursive function for extracting files or directories that will
//create any directories that do not exist along the way
//returns 0 if no error, -1 if some misc error occurs, and -2 if the 
//archive is corrupted
//takes as parameters the archive file, the path prefix and name of the file,
//which together can form fullName, the length of the file being extracted,
//and the stack of found names
//prefix + '/' + name == fullName
int extractFileRecurse(FILE* archive, char* prefix, char* name,
  const char* fullName, int fileLen, stack *found)
{
  int fullNameLen = strlen(fullName);
  char beforeSlash[fullNameLen+1];
  char afterSlash[fullNameLen+1];

  bool foundSlash =
    (parseFirstSlash(name, beforeSlash, afterSlash) == 0) ?
    true : false;

  if(!foundSlash) //file
  {
    FILE* newFile = fopen(fullName,"w");
    if(newFile == NULL) return -1;
    else
    {
      int i;
      for(i=0;i<fileLen;i++)
      {
        int c = getc(archive);
        if(c == EOF)
        {
          fclose(newFile);
          archiveCorrupted(found);
          return -2;
        }
        putc(c, newFile);
      }
      fclose(newFile);
    }
  }
  else
  {
    if(strlen(beforeSlash) == strlen(name)-1) //directory with no path
    {
      DIR *dir = opendir(fullName);
      if(dir == NULL)
      {
        if(mkdir(fullName, STDPERM) != 0) return -1;
      }
      else closedir(dir);
    }
    else //navigate path
    {
      strcat(prefix, beforeSlash);
      DIR *dir = opendir(prefix);

      if(dir == NULL)
      {
        if(mkdir(prefix, STDPERM) != 0) return -1;
        stackPush(found, prefix);
        if((dir = opendir(prefix)) == NULL) return -1;
      }
      strcat(prefix, "/");
      name = afterSlash;
      
      int j = extractFileRecurse(archive, prefix,
        name, fullName, fileLen, found);
      
      closedir(dir);
      if(j == -1) return -1;
      if(j == -2) return -2;
    }
  }
  return 0;
}

//Extracts a file or a directory and its contents
//Returns 0 if there is no error, -1 if some error occurs
//takes as parameters the archive file, the full name of the file,
//the stack of found names, and the length of the file to be extracted
int extractFile(FILE *archive, const char *fullName, stack *found, int fileLen)
{
  if(isNameInStack(found, fullName)) return 0;

  int nameLen = strlen(fullName)+1;
  char prefix[nameLen], name[nameLen];
  strcpy(name, fullName);
  prefix[0] = '\0';
  
  int j = extractFileRecurse(archive, prefix, name, fullName, fileLen, found);
  if(j == -1)
  {
    fprintf(stderr, "Failed to extract %s\n", fullName);
    stackPush(found, fullName);
    fseek(archive, fileLen, SEEK_CUR);
  }
  else if (j == -2) return -1;
  else stackPush(found, fullName);
  return 0;
}

//This method is called at the end of readArchive
//Checks if any items in the input names array were not found
//and takes appropriate action based on the mode
//Takes as parameter the stack of input names, the stack of found names, the
//name of the archive, and the mode
void checkForLeftoverNames(stack* nameStack, stack* found,
  const char *newArchiveName, char mode)
{
  //printf("checking leftovers\n");
  if (nameStack == NULL || mode == 't') return;
  
  node *temp = nameStack->head;
  while(temp != NULL)
  {
    char *name = temp->name;
    if(!isNameInStack(found, name) && !prefixOfStack(found, name))
    {
      //printf("Found %s in stack at end\n", name);
      if(mode == 'r')
      {
        bool wroteFile = false;
        fileToArchive(name, name, newArchiveName,
          found, nameStack, &wroteFile);
      }
      else if (mode == 'd') //report unable to delete
      {
        fprintf(stderr, "Could not delete file %s. "
          "Not found in archive\n", name);
      }
      else if (mode == 'x') //report unable to extract
      {
        fprintf(stderr, "Could not extract file %s. "
          "Not found in archive\n", name);
      }
    }
    temp = temp->next;
  }
}

//this method handles the actions for each mode when a filename found in
//the archive does not match any filenames from the stack of input names
//returns a bool indicating if archive is uncorrupted
//takes as parameters the archive file pointer, the temporary archive name,
//the filename that was found in the archive, and the mode
bool filenameNotMatched(FILE* archive, const char* newArchiveName,
  const char* currentName, char mode)
{
  //printf("not matched %s\n", currentName);
  int fileSize, i;
  if(fscanf(archive, "%d", &fileSize) != 1) return false;

  if(mode == 'r' || mode == 'd') //copy file over without replace or delete
  {
    //printf("copying without replace %s\n", currentName);
    FILE* newArchive = fopen(newArchiveName,"a");
    fprintf(newArchive,"%s\n%d", currentName, fileSize);
    
    //also copy '|' delimiter 
    for(i=0;i<=fileSize;i++)
    {
      int c = getc(archive);
      if (c == EOF)
      {
        fclose(newArchive);
        return false;
      }
      putc(c, newArchive);
    }

    fclose(newArchive);
  }
  else fseek(archive, fileSize+1, SEEK_CUR); //go to next file in archive
  return true;
}

//this method handles the actions for each mode when a filename found in
//the archive matches a filename from the stack of input names
//returns a bool indicating if the archive is uncorrupted
//takes as parameters the archive file pointer, the temporary archive name,
//the filename that was found in the archive, and the mode
bool filenameMatched(FILE* archive, const char* newArchiveName,
  char* currentName, stack* found, stack* nameStack, char mode)
{
  //printf("matched %s\n",currentName);
  int fileSize;
  if(fscanf(archive, "%d", &fileSize) != 1) return false;

  if(mode == 'r')
  {
    bool wroteFile = false;
    removeTrailingSlashes(currentName);
    fileToArchive(currentName, currentName,
      newArchiveName, found, nameStack, &wroteFile);
    if(!wroteFile)
    {
      //printf("did not write file %s\n", currentName);
      FILE* newArchive = fopen(newArchiveName,"a");
      fprintf(newArchive,"%s\n%d", currentName, fileSize);

      for(int i=0;i<=fileSize;i++)
      {
        int c = getc(archive);
        if (c == EOF) return false;
        putc(c, newArchive);
      }
    }
    else fseek(archive, fileSize+1, SEEK_CUR);
  }
  else if (mode == 'x')
  {
    if(getc(archive) != '|') return false;
    if(extractFile(archive, currentName, found, fileSize) != 0) return false;
  }
  else if (mode == 't')
  {
    printf("%8d %s\n", fileSize, currentName);
    fseek(archive, fileSize+1, SEEK_CUR);
  }
  else if (mode == 'd')
  {
    stackPush(found, currentName);
    fseek(archive, fileSize+1, SEEK_CUR);
  }
  return true;
}

//This method traverses the archive once and calls filenameMatched or
//filenameNotMatched when it recognizes a filename
//it takes as parameters the name of the archive, a stack of input names,
//and the mode
void readArchive(const char* archiveName, stack *nameStack, char mode)
{
  FILE *archive = fopen(archiveName,"r"); //already checked archive exists
  int c;
  char currentName[MAXLEN]; //place to hold filename being read
  int currentNameIndex = 0;
  char newArchiveName[strlen(archiveName)+10];
  char temp[MAXLEN];
  char prefix[MAXLEN];

  for(int i=0;i<MAXLEN;i++) currentName[i] = temp[i] = prefix[i] = '\0';

  createTemporaryArchiveFileIfNecessary(newArchiveName, archiveName, mode);

  //stack of the names that have been found
  stack *found = malloc(sizeof(stack));
  stackInit(found);

  while( (c = getc(archive)) != EOF)
  {
    if(c != '\n')
    {
      currentName[currentNameIndex] = c;
      currentNameIndex++;
      continue;
    }
    else //found a filename
    {
      currentName[currentNameIndex] = '\0';
      //fprintf(stderr,"  %s\n",currentName);

      for(int i=0;i<=currentNameIndex;i++) temp[i] = currentName[i];

      removeTrailingSlashes(temp);

      bool match = (nameStack == NULL);
      if(stackHasPrefix(nameStack, temp, prefix))
      {
        struct stat buf;
        //check if containing directory can be opened
        if(mode == 'r')
        {
          if(lstat(prefix, &buf) != 0) match = false;
          else match = true;
        }
        else match = true;
      }
      match = match || isNameInStack(nameStack, temp);

      currentNameIndex = 0; //get ready to read another name

      if(match)
      {
        if(!filenameMatched(archive, newArchiveName, currentName, found,
          nameStack, mode))
        {
          fclose(archive);
          archiveCorrupted(found);
          return;
        }
      }
      else
      {
        if(!filenameNotMatched(archive, newArchiveName, currentName, mode))
        {
          fclose(archive);
          archiveCorrupted(found);
          return;
        }
      }
    }
  }

  if(currentNameIndex != 0)
  {
    fclose(archive);
    archiveCorrupted(found);
    return;
  }

  checkForLeftoverNames(nameStack, found, newArchiveName, mode);

  fclose(archive);

  if(mode == 'd' || mode == 'r') rename(newArchiveName,archiveName);

  freeStack(found);
  free(found);
}

//Replaces trailing slashes in the input with nulls. Also puts all the names
//into a stack
//Takes as parameters the input array of names, the length of that array,
//and the stack that will hold the names
void cleanInput(char *names[], int namesLen, stack *s)
{
  int i;
  for(i=0;i<namesLen;i++)
  {
    if( strcmp(names[i], "/") != 0) removeTrailingSlashes(names[i]);
    if(!isNameInStack(s, names[i])) stackPush(s, names[i]);
  }
}

//prints usage information message and exits
void usageHelp()
{
  const char *usageString = "Far: Far r|x|d|t archive [filename]*\n";
  FARFAIL("%s", usageString);
}

//verifies that there are the correct number and type of arguments to main
//takes as parameters argc and argv from main
char verifyInputFormat(int argc, char *argv[])
{
  if(argc < 3) usageHelp();
  if(strlen(argv[1]) != 1) usageHelp();
  char mode = argv[1][0];
  if(mode != 'r' && mode != 'd' && mode != 't' && mode != 'x') usageHelp();
  return mode;
}

//ensures that an archive file exists or else fails. creates an archive in the
//case of 'r'
//takes as input the archive name and the mode
void verifyArchiveExists(char* archiveName, char mode)
{
  FILE *archive = fopen(archiveName,"r");
  if(archive != NULL) fclose(archive);
  else
  {
    if (mode == 'r') //create empty archive file
    {
      archive = fopen(archiveName,"w");
      fclose(archive);
    }
    else FARFAIL("Error: archive file %s does not exist\n", archiveName);
  }
  struct stat buf;
  lstat(archiveName, &buf);
  if(!S_ISREG(buf.st_mode))
    FARFAIL("Archive %s is not a regular file. Shutting down.\n",archiveName);
}

//after cleaning the input and verifying that the input arguments are correct,
//main runs readArchive with arguments appropriate for the mode, and
//then cleans up
int main(int argc, char *argv[])
{
  char mode = verifyInputFormat(argc, argv);
  
  verifyArchiveExists(argv[2], mode);

  stack *nameStack = malloc(sizeof(stack));
  stackInit(nameStack);

  cleanInput(argv+3, argc-3, nameStack);

  if(mode == 'r' || mode == 'd')
  {
    if(nameStack->size == 0) return EXIT_SUCCESS;
    else readArchive(argv[2], nameStack, mode);
  }
  else if (mode == 'x')
  {
    if(nameStack->size == 0) readArchive(argv[2], NULL, mode);
    else readArchive(argv[2], nameStack, mode);
  }
  else if (mode == 't') readArchive(argv[2], NULL, mode);

  freeStack(nameStack);
  free(nameStack);

  return EXIT_SUCCESS;
}

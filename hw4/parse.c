#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include "parse.h"

CMD * validateCMD(token *start);

char *errorMsg;
//global variable for all here docs

void setErrorMsg(char *s)
{
  if(errorMsg != NULL) free(errorMsg);
  errorMsg = strdup(s);
}

void safeFreeCMD (CMD *c)
{
    if (!c) return;

    if(c->argv)
    {
      for (char **p = c->argv;  *p;  p++)
      {
        if(p) free (*p);
      }
      free (c->argv);
    }

    if(c->fromFile) free (c->fromFile);
    if(c->toFile) free (c->toFile);
    
    freeCMD (c->left);
    freeCMD (c->right);

    free (c);
}

void printList(token *tok)
{
  while(tok != NULL)
  {
    printf("%d %s\n", tok->type, tok->text);
    tok = tok->next;
  }
  printf("\n");
}

bool verifyParens(token *tok)
{
  int leftParen = 0;
  while(tok != NULL)
  {
    if(tok->type == PAR_LEFT) leftParen++;
    else if(tok->type == PAR_RIGHT)
    {
      if(leftParen-- <= 0) return false;
    }
    tok = tok->next;
  }
  
  return (leftParen == 0);
}

//remove a redirect and filename from a linked list
bool removeRedirect(token **tok, int c)
{
  token *prev = NULL;
  token *start = *tok;
  while(start != NULL)
  {
    if(start->type == c)
    {
      if(!prev)
      {
        *tok = (start->next)->next;
      }
      else prev->next = (start->next)->next;
      return true;
    }
    prev = start;
    start = start->next;
  }
  return false;
}

//search linked list starting at start for a token of type c
token* listSearch(token *start, int c)
{
  while(start != NULL)
  {
    if(start->type == c) return start;
    start = start->next;
  }
  return NULL;
}

int listCount(token *start, int argc, int argv[])
{
  int count = 0;
  while(start != NULL)
  {
    for(int i=0;i<argc;i++)
    {
      if(start->type == argv[i]) count++;
    }
    start = start->next;
  }
  return count;
}

//return the first character in argv found in the linked list starting at start
//return -1 if none found
int listCharSearch(token *start, int argc, int argv[])
{
  while(start != NULL)
  {
    for(int i=0;i<argc;i++)
    {
      if(start->type == argv[i]) return argv[i];
    }
    start = start->next;
  }
  return -1;
}

//Copy a list and return pointer to head
token * copyList(token *tok)
{
  token *newList = NULL, *current = NULL;

  while(tok != NULL)
  {
    token *newTok = malloc(sizeof(token));
    *newTok = *tok;
    if(newList == NULL)
    {
      newList = current = newTok;
    }
    else
    {
      current->next = newTok;
      current = newTok;
    }
    tok = tok->next;
  }
  
  if(current) current->next = NULL;
  return newList;
}

/*
//Splits the list at the first occurrence of any character in argv
//Given a list A->B->C, if B has a char in argv, A is modified to point to NULL
//and C is returned, with the char of B in foundChar

//foundChar is NONE if no char in argv is found. NULL is returned.
//foundChar is ERROR if a char in argv is found at the beginning of the list.
//NULL is returned
//if a character is found, foundChar is set to that character and a pointer to
//the next node is returned. The list is also modified.
//Ignores characters inside parentheses
*/
token * splitList(token *tok, int argc, int argv[], int *foundChar)
{
  token *prev = NULL;
  token *temp = tok;
  int leftParen = 0;
  
  while(temp != NULL)
  {
    if(temp->type == PAR_LEFT) leftParen++;
    if(temp->type == PAR_RIGHT) leftParen--;

    for(int i=0;i<argc;i++)
    {
      if(leftParen > 0) break;
      if(argv[i] == temp->type)
      {
        if(temp == tok)
        {
          *foundChar = ERROR;
          return NULL;
        }
        else
        {
          *foundChar = argv[i];
          prev->next = NULL;
          return temp->next;
        }
      }
    }
    prev = temp;
    temp = temp->next;
  }
  *foundChar = NONE;
  return NULL;
}

CMD * validateSimple(token *start)
{
  //no parens allowed
  if(listSearch(start, PAR_LEFT) != NULL ||
     listSearch(start, PAR_RIGHT) != NULL)
  {
    setErrorMsg("Parse: command and subcommand");
    return NULL;
  }
  //should be no PIPE or SEP chars already
  
  CMD *ans = mallocCMD();
  ans->type = SIMPLE;
  
  int inputs[2] = {RED_IN, RED_HERE};
  int numInputs = listCount(start, 2, inputs);
  if(numInputs > 1)
  {
    safeFreeCMD(ans);
    setErrorMsg("Parse: two input redirects");
    return NULL;
  }
  else if(numInputs == 1)
  {
    int type = listCharSearch(start, 2, inputs);
    token *temp = listSearch(start, type);
    ans->fromType = type;
    temp = temp->next;
    if(temp && temp->type == SIMPLE) //proper filename follows redirect
    {
      ans->fromFile = strdup(temp->text);
      //TODO: account for here documents
      removeRedirect(&start, type);
      
    }
    else
    {
      safeFreeCMD(ans);
      setErrorMsg("Parse: missing filename");
      return NULL;
    }
  }

  int outputs[8] = {RED_OUT, RED_OUT_C, RED_OUT_APP, RED_OUT_APP_C, RED_ERR,
      RED_ERR_C, RED_ERR_APP, RED_ERR_APP_C};
  int numOutputs = listCount(start, 8, outputs);
  if(numOutputs > 1)
  {
    safeFreeCMD(ans);
    setErrorMsg("Parse: two output redirects");
    return NULL;  
  }
  else if(numOutputs == 1)
  {
    int type = listCharSearch(start, 8, outputs);
    token *temp = listSearch(start, type);
    ans->toType = type;
    temp = temp->next;
    if(temp && temp->type == SIMPLE) //proper filename follows redirect
    {
      ans->toFile = strdup(temp->text);
      removeRedirect(&start, type);

    }
    else
    {
      safeFreeCMD(ans);
      setErrorMsg("Parse: missing filename");
      return NULL;
    }
  }
  
  //get remaining commands, must be at least one
  
  //TODO: error somewhere below here
  token *current = start;
  int len=0;
  while(current != NULL)
  {
    assert(current->type == SIMPLE);
    current = current->next;
    len++;
  }
  if(ans->argv) free(ans->argv); //TODO: figure out why this causes error
  
  ans->argv = calloc(len+1, sizeof(char *));
  ans->argc = len;
  current = start;
  
  int i=0;
  while(current != NULL)
  {
    ans->argv[i] = strdup(current->text);
    current = current->next;
    i++;
  }
  ans->argv[i] = NULL;

  return ans;
}

CMD * validateStage(token *start)
{
  if(!verifyParens(start)) return NULL; //TODO: add error message

  if(start->type == PAR_LEFT) //subcommand
  {
    token *temp = start->next;
    token *prev = start;
    //make sure last token has type PAR_RIGHT
    while(temp->next != NULL)
    {
      prev = temp;
      temp = temp->next;
    }
    if(temp->type != PAR_RIGHT) return NULL; //TODO: add error message
    //command and subcommand
    else //eliminate left and right parens and validate command
    {
      start = start->next; 
      prev->next = NULL;
      CMD *leftCMD = validateCMD(start);
      if(!leftCMD) return NULL;
      else
      {
        CMD *cmd = mallocCMD();
        cmd->type = SUBCMD;
        cmd->left = leftCMD;
        cmd->right = NULL;
        return cmd;
      }
    }
  }
  else
  {
    return validateSimple(start);
  }
}

CMD * validatePipeline(token *start)
{
  if(!verifyParens(start)) return NULL; //TODO: add error message
  int args[2] = {PIPE, PIPE_ERR};
  int c;
  token *postSep = splitList(start, 2, args, &c);
  if(c == PIPE || c == PIPE_ERR)
  {
    if(postSep == NULL)
    {
      //set error message
      return NULL;
    }
    
    CMD *leftCMD = validateStage(start); //TODO: account for extra redirect
    if(!leftCMD) return NULL;
    
    CMD *rightCMD = validatePipeline(postSep);
    if(!rightCMD)
    {
      freeCMD(leftCMD);
      //set error message
      return NULL;
    }
    
    CMD *cmd = mallocCMD();
    cmd->type = c;
    cmd->left = leftCMD;
    cmd->right = rightCMD;

    return cmd;
  }
  else if(c == NONE)
  {
    return validateStage(start); //TODO: no extra redirect
  }
  else //c == ERROR
  {
    //set error message
    return NULL;
  }
}

CMD * validateAndOr(token *start)
{
  if(!verifyParens(start)) return NULL; //TODO: add error message
  int args[2] = {SEP_AND, SEP_OR};
  int c;
  token *postSep = splitList(start, 2, args, &c);
  if(c == SEP_AND || c == SEP_OR)
  {
    if(postSep == NULL)
    {
      //set error message
      return NULL;
    }
    
    CMD *leftCMD = validatePipeline(start);
    if(!leftCMD) return NULL;
    
    CMD *rightCMD = validateAndOr(postSep);
    if(!rightCMD)
    {
      freeCMD(leftCMD);
      //set error message
      return NULL;
    }
    
    CMD *cmd = mallocCMD();
    cmd->type = c;
    cmd->left = leftCMD;
    cmd->right = rightCMD;

    return cmd;
  }
  else if(c == NONE)
  {
    return validatePipeline(start);
  }
  else //c == ERROR
  {
    //set error message
    return NULL;
  }
}

CMD * validateCMD(token *start)
{
  if(!verifyParens(start)) return NULL; //TODO: add error message
  
  int args[2] = {SEP_BG, SEP_END};
  int c;
  token *postSep = splitList(start, 2, args, &c);

  if(c == SEP_BG || c == SEP_END)
  {
    CMD *leftCMD = validateAndOr(start);
    if(!leftCMD) return NULL;
    
    CMD *rightCMD;
    if(postSep != NULL)
    {
      rightCMD = validateCMD(postSep);
      if(!rightCMD)
      {
        freeCMD(leftCMD);
        //set error message
        return NULL;
      }
    }
    else rightCMD = NULL;
    
    CMD *cmd = mallocCMD();
    cmd->type = c;
    cmd->left = leftCMD;
    cmd->right = rightCMD;

    return cmd;
  }
  else if(c == NONE)
  {
    return validateAndOr(start);
  }
  else //c == ERROR
  {
    //set error message
    return NULL;
  }
}

// Parse a token list into a command structure and return a pointer to
// that structure (NULL if errors found).
CMD * parse (token *tok)
{
  errorMsg = NULL;
  token *temp;
  temp = copyList(tok);
  CMD *ans = validateCMD(temp);
  //printf("done parsing\n");
  if(!ans && errorMsg)
  {
    fprintf(stderr, "%s\n", errorMsg);
    free(errorMsg);
  }

  //TODO: deal with freeing memory of copied list
  return ans;
}


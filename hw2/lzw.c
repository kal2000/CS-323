/*
 lzw.c -- LZW Compression algorithm
 Kevin Lai (10/5/2012)
 
file compression and decompression filters:
 encode [-m MAXBITS] [-p WINDOW] [-e]

 decode

Compresses and decompresses files using the LZW algorithm. Allows for pruning of the
string table, setting maxbits, and starting the string table without one-character
strings 

Uses a small section of code from the man page for strtol
	https://www.kernel.org/doc/man-pages/online/pages/man3/strtol.3.html
*/
#define _GNU_SOURCE
#include "/c/cs323/Hwk2/code.h"
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#define DIE(format,value) fprintf(stderr,format,value), exit(EXIT_FAILURE)
#define DIE2(format,value,val2) fprintf(stderr,format,value,val2), exit(EXIT_FAILURE)
#define DEFAULTMAXBITS (12) //Default value for maxbits
#define MINMAXBITS (CHAR_BIT+1) //Minimum allowed value for maxbits
#define MAXMAXBITS (3*CHAR_BIT) //Maximum allowed value for maxbits
#define NUMSINGLECODES (256) //Number of single-character codes
#define NUMSPECIALCODES (6) //Number of special codes used
#define EMPTYCODE (0) //Code for EMPTY
#define ESCAPECODE (1) //Code for ESCAPE
#define INCR_NBITSCODE (2) //Code for increasing number of bits
#define PRUNECODE (3) //Code for pruning
#define SPACERCODE (4) //Code for no output
#define EOFCODE (5) // Code for EOF
#define PRUNING_FLAG (2) // Flag indicating pruning occurred and insert success
#define NBITS_FLAG (3) //Flag indicating number of bits increased and insert success

typedef long long lint;
typedef unsigned int uint;

//stores information about the flags to encode
//bools e, m, p corresponding to the flags to encode
//mode contains 'e' or 'd', depending on if called as encode or decode
//window and maxbits hold the corresponding parameters to encode
typedef struct inputFlags_t
{
  bool e, m, p;
  long maxBits, window;
  int mode;
} inputFlags;

//mallocs and returns a pointer to an inputflags struct with the default maxbits set
inputFlags * inputFlagsCreate()
{
  inputFlags *flags = malloc(sizeof(inputFlags));
  flags->e = flags->m = flags->p = false;
  flags->maxBits = DEFAULTMAXBITS;
  flags->window = 0;
  return flags;
}

//array of ints *str that keeps track of its size in "size"
typedef struct intstr_t
{
	int *str;
	int size;
} intstr;

//mallocs and returns a pointer to an intstr with zero-length and a NULL array
intstr * intstrCreate()
{
	intstr *s = malloc(sizeof(intstr));
	s->str = NULL;
	s->size = 0;
	return s;
}

//frees intstr s and its array of ints
void freeIntstr(intstr *s)
{
	if(s == NULL) return;
	if(s->str != NULL) free(s->str);
	free(s);
}

//node for stack or linked list
//c is a character, *next is a pointer to another node
typedef struct node_t 
{
  int c;
  struct node_t *next;
} node;

//stack struct
//*head is a pointer to the head node of the stack
//"size" is an int containing the stack size
typedef struct stack_t
{
  node *head;
  int size;
} stack;

//mallocs and returns a pointer to a stack of size zero with a NULL head ptr
stack * stackCreate()
{
	stack *s = malloc(sizeof(stack));
	s->size = 0;
	s->head = NULL;
	return s;
}

//mallocs a node with the int c and pushes it to stack s
void stackPush(stack *s, int c)
{
  node *n = malloc(sizeof(node));
	n->c = c;
  if(s->size == 0) n->next = NULL;
  else n->next = s->head;
  s->head = n;
  s->size++;
}

//pops and frees a node from the stack s and returns the int value in the node
int stackPop(stack *s)
{
	if(s == NULL || s->head == NULL || s->size == 0) return -1;
	int c = s->head->c;
	node *temp = s->head;
	s->head = s->head->next;
	free(temp);
	s->size--;
	return c;
}

//frees all nodes in the stack s as well as the stack itself
void freeStack(stack *s)
{
	if(s == NULL) return;
  node *temp = s->head;
  while(temp!=NULL)
  {
    s->head = s->head->next;
    free(temp);
    temp = s->head;
  }
  free(s);
}

/* p is the code of the prefix of entry
k is the character of entry
code is the code for the (prefix, char) pair (p, k)
lastUse stores the last time the code of an entry was seen
If lastUse is i, the current code was the i'th code sent or received.
If lastUse is 0, the code hasn't been seen since it was added */
typedef struct entry_t
{
  int p;
  int k;
  int code;
  lint lastUse;
} entry;

//mallocs and returns a pointer to an entry initialized with p, k, code, and lastUse
entry * entryCreate(int p, int k, int code, lint lastUse)
{
  entry *e = malloc(sizeof(entry));
  e->p = p;
  e->k = k;
  e->code = code;
  e->lastUse = lastUse;
  return e;
}

/*hash table
numBits is the number of bits used to represent codes currently
maxCode is the maximum code allowed in the hash
size is the size of the hash
numEntries is the current number of entries in the hash table
entries ** is the table of entries */
typedef struct hash_t
{
  int numBits;
  uint maxCode, size, numEntries;
  entry **entries;
} hash;

/* hash and array
h is a hash table
codeTable is an array of entries indexed by codes
e and p indicate if the hashArray was created with the e or p flags specified
window and maxBits store the parameters that the hashArray was initialized with
Note: h and codeTable contain pointers to the same entries, but they
are indexed differently */
typedef struct hashArray_t
{
	bool e;
	bool p;
	long window, maxBits;
  hash *h;
  entry **codeTable;
} hashArray;

hashArray * prune(hashArray *, lint);
void logTable(hashArray *, const char *);
bool updateLastUse(hashArray *, int, lint);

/////////////////////////////////////////////////////////////////////
//////////////////////                        ///////////////////////
//////////////////////       HASH TABLE       ///////////////////////
//////////////////////                        ///////////////////////
/////////////////////////////////////////////////////////////////////


//counts the number of bits required to represent the int numEntries
//returns offset plus that number
//offset should be 0 or 1. This is to allow decode to use the sanity check
int countNumBits(int numEntries, int offset)
{
	if(numEntries < 0) return -1;
	int i=0;
	while(((numEntries-1)>>i) > 0)
	{
		i++;
	}
	if(numEntries == 0) return 1+offset;
	return i+offset;
}

//Hash function using prefix code and character code
//returns an unsigned int
uint hashFunction(hash *h, int p, int k)
{
  return ((unsigned)(p) << CHAR_BIT | (unsigned) (k)) % (h->size);
}

//returns a 0 if no free codes exist in hash h
//returns NBITS_FLAG if increased numbits and free codes exist
//else returns a 1, indicating free codes exist in h
int existFreeCodes(hash *h)
{
  if(h->numEntries <= h->maxCode)
  {
    uint currentNumCodes = 1 << h->numBits;
    if(h->numEntries >= currentNumCodes) //maxCode fits in maxBits bits
    {
      h->numBits++;
      return NBITS_FLAG;
    }
    return 1;
  }
  else return 0;
}

/* Searches hash h using linear probing given inputs of prefix code p and
character code k
returns a pointer to the entry corresponding to (p, k) or else NULL if (p, k)
is not in the hash
*/
entry * hashSearch(hash *h, int p, int k)
{
  uint key = hashFunction(h, p, k);
  uint original = key;
  uint size = h->size;
  entry **entries = h->entries;
  while(entries[key] != NULL)
  {
    if(entries[key]->p == p && entries[key]->k == k)
    {
      return entries[key];
    }
    else
    {
      if(++key >= size) key = 0;
      if(key == original) return NULL;
    }
  }
  return NULL;
}

/* inserts a (p, k) pair into hash h as an entry. The entry of (p, k) is
assigned a code in the table and lastUse of the entry is set to 0
returns 0 if the insert fails, 1 if normal success, 2 if success and the number of
bits needed to represent all the codes increased */
int hashInsert(hash *h, int p, int k)
{
  if(h->numEntries == h->size)
  {
    return 0;
  }
  if(hashSearch(h,p,k) != NULL) return 1;
  uint key = hashFunction(h, p, k);
  uint original = key;
  uint size = h->size;
  entry **entries = h->entries;
  while(entries[key] != NULL)
  {
    if(++key >= size) key = 0;
    if(key == original)
    {
      return 0; //table full
    }
  }

	int x;
  if((x = existFreeCodes(h)))
  {
    entry *e = entryCreate(p, k, h->numEntries++, 0);
    entries[key] = e;
    return x;
  }
  else
  {
    return 0;
  }
}

//Mallocs a hash with size = 1+2^(maxBits+1), maxCode = 2^(maxBits) - 1
//numBits set to 1, numEntries set to 0
//returns a pointer to the hash
hash * hashCreate(int maxBits)
{
  hash *h;
  uint maxCode = (1 << maxBits) - 1;
  uint size = 1 + (1 << (maxBits+1));
  if(size > 0)
  {
    h = malloc(sizeof(hash));
    h->size = size;
    h->numEntries = 0;
    h->maxCode = maxCode;
    h->numBits = 1; //will be changed when hash Array initializes
    h->entries = calloc(size, sizeof(entry*));
    for(int i=0;i<size;i++) h->entries[i] = NULL;
    return h;
  }
  else return NULL;
}

//frees all entries in an array "entries" of length len
void freeEntries(entry **entries, int len)
{
	if(entries == NULL) return;
	for(int i=0;i<len;i++)
	{
		if(entries[i] == NULL) continue;
		else free(entries[i]);
	}
	free(entries);
}

//frees a hash table h and its entries
void freeHash(hash *h)
{
	if(h == NULL) return;
	freeEntries(h->entries, h->size);
	free(h);
}

/////////////////////////////////////////////////////////////////////
//////////////////////                        ///////////////////////
//////////////////////   HASH ARRAY STRUCT    ///////////////////////
//////////////////////                        ///////////////////////
/////////////////////////////////////////////////////////////////////

//Searches the entries in the hash array ha using a (prefix, char) pair (p, k)
//Calls hashSearch on the hash in ha
//returns a pointer to the entry if it is in the table, else returns NULL
entry * haHashSearch(hashArray *ha, int p, int k)
{
  return hashSearch(ha->h, p, k);
}

//Searches to see if an entry with code "code" is in the hashArray by looking up
//ha->codeTable[code]
//returns a pointer to the entry if it is in the table, else returns NULL
entry * haCodeSearch(hashArray *ha, int code)
{
  return ha->codeTable[code] != NULL ? ha->codeTable[code] : NULL;
}

/* Inserts a (p, k) pair into the hashArray ha. If this insertion causes pruning
to be required, PRUNING_FLAG is returned. If the insertion caused numBits to increase,
returns NBITS_FLAG. Else, if the insertion succeeds, returns 1.
If insertion fails, returns 0.
time represents the number of codes sent or received up to this point */
int haInsert(hashArray *ha, int p, int k, lint time)
{
	int x;
  if((x = hashInsert(ha->h, p, k)))
  {
    entry *e = hashSearch(ha->h, p, k);
    ha->codeTable[e->code] = e;
    if(x == NBITS_FLAG) return NBITS_FLAG;
    //impossible to have increased bits and also need to prune
    if((ha->h->maxCode < ha->h->numEntries) && ha->p) //this only occurs for encode
		{
			return PRUNING_FLAG;
    }
    return 1;
  }
  else return 0;
}

/* Same as haInsert except that the inserted element is entered with lastUse
equal to time.
haInsert description:
Inserts a (p, k) pair into the hashArray ha. If this insertion causes pruning
to be required, PRUNING_FLAG is returned. If the insertion caused numBits to increase,
returns NBITS_FLAG. Else, if the insertion succeeds, returns 1.
If insertion fails, returns 0.
time represents the number of codes sent or received up to this point */
int haInsertWithLastUse(hashArray *ha, int p, int k, lint time)
{
	int x;
  if((x = hashInsert(ha->h, p, k)))
  {
    entry *e = hashSearch(ha->h, p, k);
    ha->codeTable[e->code] = e;
    e->lastUse = time; //only line different from haInsert
    if(x == NBITS_FLAG) return NBITS_FLAG;
    //impossible to have increased bits and also need to prune
    if((ha->h->maxCode < ha->h->numEntries) && ha->p) //this only occurs for encode
		{
			return PRUNING_FLAG;
    }
    return 1;
  }
  else return 0;
}

//Adds all one character strings from 0 to 255 to the hashArray ha
void haAddOneCharStrings(hashArray *ha)
{
  for(int i=0; i<NUMSINGLECODES; i++) haInsert(ha,0,i,0);
}

//Adds all special codes and the empty string to the hashArray ha
void haAddEssentialCodes(hashArray *ha)
{
	for(int i=0;i<NUMSPECIALCODES;i++) haInsert(ha, 0, -1-i, 0);
}

//Creates a hashArray using the parameters in the inputFlags struct F
//Mallocs and returns a pointer to a hashArray
hashArray * hashArrayCreate(inputFlags *f)
{
  hashArray *ha = malloc(sizeof(hashArray));
  ha->h = hashCreate(f->maxBits);
  ha->codeTable = calloc(ha->h->size, sizeof(entry));
  haAddEssentialCodes(ha);
  ha->e = f->e;
  ha->p = f->p;
  ha->maxBits = f->maxBits;
  ha->window = 0;
  if(ha->p) ha->window = f->window;
  if(!ha->e) haAddOneCharStrings(ha);
  return ha;
}

//frees the hash and codeTable in HA and then frees HA
void freeHashArray(hashArray *ha)
{
	if(ha == NULL) return;
	freeHash(ha->h);
	if(ha->codeTable != NULL) free(ha->codeTable);
	free(ha);
}

/*Searches the hashArray ha to see if it contains the (prefix, char) pair
representing the intstr "is". This is done by searching (prefix, char) pairs
sequentially from the start of the string to the end. Returns a pointer to the
entry containing the (prefix, char) pair representing "is" if found,
else returns NULL */
entry * haIntstrSearch(hashArray *ha, intstr *is)
{
	int p = 0;
	entry *e;
	for(int i=0;i<is->size;i++)
	{
		if((e = haHashSearch(ha, p,is->str[i])) == NULL) return NULL;
		p = e->code;
	}
	return e;
}

//Pops elements from stack S and puts them in that order into an intstr
//Mallocs and returns a pointer to an intstr containing the chars from S
intstr * intstrFromStack(stack *s)
{
	if(s == NULL) return NULL;
	int size = s->size;
	int *str = calloc(size,sizeof(int));
	for(int i=0;i<size;i++) str[i] = stackPop(s);
	intstr *is = intstrCreate();
	is->str = str;
	is->size = size;
	return is;
}

//Expands the string represented by CODE in HA using a stack and the method
//intstrFromStack.
//Mallocs and returns a pointer to an intstr containing the expanded string
intstr * intstrFromCode(hashArray *ha, int code)
{
	if(!haCodeSearch(ha, code)) return NULL;
	stack *s = stackCreate();
	entry *e;
	do
	{
		e = haCodeSearch(ha, code);
		code = e->p;
		stackPush(s, e->k);
	}
	while(e->p != 0);
	intstr *is = intstrFromStack(s);
	freeStack(s);
	return is;
}

//updates the lastUse of the entry with code CODE in HA if newLastUse is
//more recent than the lastUse of the entry
//Returns true if found an entry with code CODE, else returns false
bool updateLastUse(hashArray *ha, int code, lint newLastUse)
{
	entry *e = haCodeSearch(ha, code);
	if(e == NULL) return false;
	else
	{
		if(newLastUse > e->lastUse) e->lastUse = newLastUse;
		return true;
	}
}

//Inserts intstr IS into HA and sets the lastUse of the (prefix, char) pair
//representing the whole string to newLastUse. Used during pruning to transfer
//strings and their prefixes to the new hash array. Loops through IS and inserts
//(prefix, char) pairs in order at the time TIME. Returns true if the insert was
//successful, else returns false if an error occured
bool haIntstrInsert(hashArray *ha, intstr *is, lint newLastUse, lint time)
{
	if(is->size == 0) return true;
	int p = 0;
	int *str = is->str;
	entry *e;
	for(int i=0;i<is->size;i++)
	{
		if((e = haHashSearch(ha, p, str[i])) != NULL)
		{
			p = e->code;
		}
		else
		{
			if(haInsert(ha, p, str[i], time) == 0) break;
			e = haHashSearch(ha, p, str[i]);
			if(e == NULL)
			{
				return false;
			}
			p = e->code;
		}
	}
	if(!updateLastUse(ha, p, newLastUse)) return false;
	return true;
}

/////////////////////////////////////////////////////////////////////
//////////////////////                        ///////////////////////
//////////////////////        PRUNING         ///////////////////////
//////////////////////                        ///////////////////////
/////////////////////////////////////////////////////////////////////

//Generates an inputFlags struct from a hashArray, setting the appropriate fields
//Mallocs and returns a pointer to an inputFlags struct
inputFlags * inputFlagsFromHA(hashArray *ha)
{
	if(ha == NULL) return NULL;
	inputFlags *f = inputFlagsCreate();
	f->e = ha->e;
	f->p = ha->p;
	f->maxBits = ha->maxBits;
	f->window = ha->window;
	return f;
}

//Prunes the hash array OLDHA based on TIME. Creates a new hashArray newha and
//copies all strings whose lastUse is within window of TIME to newha using 
//haIntstrInsert to recursively copy all prefixes as well. Also copies all
//one-character strings if the -e is not set. Always adds all special codes to
//newha first. Frees the old hashArray and returns a pointer to newha
hashArray * prune(hashArray *oldha, lint time)
{
	inputFlags *f = inputFlagsFromHA(oldha);
	hashArray *newha = hashArrayCreate(f);
	free(f);
	
	int len = oldha->h->size;
	entry **table = oldha->codeTable;

	for(int i=0;i<len;i++)
	{
		entry *e = table[i];
		if(e != NULL)
		{
			lint lastUse = e->lastUse;
			if(lastUse != 0 && (time - oldha->window < lastUse))
			{
				intstr *is = intstrFromCode(oldha, e->code);
				haIntstrInsert(newha, is, lastUse, time);
				freeIntstr(is);
			}
		}
		else break;
	}
	freeHashArray(oldha);
	return newha;
}

//increments TIME and ensures that TIME never exceeds the size of LLONG_MAX/2
//Takes HA as a parameter in case lastUse times must be adjusted
//Returns the new value of TIME
lint safeTimeIncrement(hashArray *ha, lint time)
{
	time++;
	lint newTime = time;
	if(time > LLONG_MAX/2)
	{	
		long window = ha->window;
		if(window < LONG_MAX/10) window = LONG_MAX/10;
		newTime = time - window;
		if(newTime - window <= 0) return time; //newTime must be greater than window
		
		entry **entries = ha->h->entries;
		for(int i=0;i<ha->h->size;i++)
		{
			if(entries[i] == NULL) continue;
			if(entries[i]->lastUse < newTime) entries[i]->lastUse = 0;
			else entries[i]->lastUse -= window;
		}
	}
	return newTime;
}

/////////////////////////////////////////////////////////////////////
//////////////////////                        ///////////////////////
//////////////////////        ENCODE          ///////////////////////
//////////////////////                        ///////////////////////
/////////////////////////////////////////////////////////////////////

//Outputs MAXBITS:WINDOW:EFLAG: to stdout so decode knows how to set its
//string table.
void sendParameters(hashArray *ha)
{
	printf("%ld:%ld:%d:", ha->maxBits, ha->window, ha->e);
}

//Runs the encode part of the LZW algorithm, using HA as the string table
//First calls sendParameters. Afterwards, reads characters from stdin and
//uses putBits to output codes as a bitstream to stdout
void encode(hashArray *ha)
{
	sendParameters(ha);
	int c = EMPTYCODE, k;
	entry *e;
	long long time = 0;
	while((k = getchar()) != EOF)
	{
		if(ha->e && ((e = haHashSearch(ha, EMPTYCODE, k)) == NULL)) //new character
		{
			if(c != EMPTYCODE) //put old code
			{
				putBits(ha->h->numBits, c);
				time = safeTimeIncrement(ha, time);
				updateLastUse(ha, c, time);
			}
			putBits(ha->h->numBits, ESCAPECODE);
			time = safeTimeIncrement(ha, time);
			updateLastUse(ha, ESCAPECODE, time);
			
			putBits(CHAR_BIT, k);
			time = safeTimeIncrement(ha, time);

			int x = haInsertWithLastUse(ha, EMPTYCODE, k, time);
			if(x == PRUNING_FLAG)
			{
				putBits(ha->h->numBits, PRUNECODE);
				time = safeTimeIncrement(ha, time);
				updateLastUse(ha, PRUNECODE, time);
				
				putBits(ha->h->numBits, SPACERCODE); //indicates new char just added
				time = safeTimeIncrement(ha, time);
				updateLastUse(ha, SPACERCODE, time);
	
				ha = prune(ha, time);
			}
			//Both tables should automatically increment if this code pushes them over

			c = EMPTYCODE;
		}
		else if((e = haHashSearch(ha, c, k)) != NULL)
		{
			c = e->code;
		}
		else
		{
			putBits(ha->h->numBits, c);
			time = safeTimeIncrement(ha, time);
		
			updateLastUse(ha, c, time);

			int x = haInsert(ha, c, k, time);

			if(x == PRUNING_FLAG)
			{
				putBits(ha->h->numBits, PRUNECODE);
				time = safeTimeIncrement(ha, time);
				updateLastUse(ha, PRUNECODE, time);

				entry *temp = haHashSearch(ha, EMPTYCODE, k);
				putBits(ha->h->numBits, temp->code);
				time = safeTimeIncrement(ha, time);
				updateLastUse(ha, temp->code, time);
				
				ha = prune(ha, time);
				
				c = EMPTYCODE; //start over with code after pruning
			}
			else
			{
				if(x == NBITS_FLAG)
				{
					putBits(ha->h->numBits-1, INCR_NBITSCODE);
					time = safeTimeIncrement(ha, time);
					updateLastUse(ha, INCR_NBITSCODE, time);
				}
				entry *temp = haHashSearch(ha, EMPTYCODE, k);
				c = temp->code;
			}
		}
	}
	if(c != EMPTYCODE) 
	{
		putBits(ha->h->numBits, c);
		time = safeTimeIncrement(ha, time);
		updateLastUse(ha, c, time);
	}
	putBits(ha->h->numBits, EOFCODE);
	time = safeTimeIncrement(ha, time);
	updateLastUse(ha, EOFCODE, time);
	flushBits();
	freeHashArray(ha);
}

/////////////////////////////////////////////////////////////////////
//////////////////////                        ///////////////////////
//////////////////////        DECODE          ///////////////////////
//////////////////////                        ///////////////////////
/////////////////////////////////////////////////////////////////////

//Runs at the beginning of decode to read the parameters MAXBITS:WINDOW:EFLAG:
//sent by encode. If the parameters are not formatted properly, the code exits.
//Else, this method mallocs and returns a pointer to a hashArray initialized with
//the parameters retrieved from encode
hashArray * readParameters()
{
	long maxBits, window, e;
	bool fail = false;
	if(scanf("%ld", &maxBits) == EOF) fail = true;
	else
	{
		if(!fail && (getchar() != ':')) fail = true;
		if(!fail && (scanf("%ld", &window) == EOF)) fail = true;
		if(!fail && (getchar() != ':')) fail = true;
		if(!fail && (scanf("%ld", &e) == EOF)) fail = true;
		if(!fail && (getchar() != ':')) fail = true;
	}
	if(!fail)
	{
		if(maxBits < MINMAXBITS || maxBits > MAXMAXBITS) fail = true;
		if(window < 0) fail = true;	
		if(e != 0 && e != 1) fail = true;
	}

	if(fail)
	{
		DIE("decode: invalid input%s\n","");
	}
	else
	{
		inputFlags *flags = inputFlagsCreate();
		flags->maxBits = maxBits;
		if(window != 0)
		{
			flags->p = true;
			flags->window = window;
		}
		flags->e = e;
		hashArray *ha = hashArrayCreate(flags);
		free(flags);
		return ha;
	}
	return NULL; //this should never execute;
}

/* Detects if CODE is a special code and takes appropriate action. Will set
oldC, finalK, and timeptr to ensure that decode runs properly after the
special code is handled. May alter the hashArray sent via PHA if pruning
is required. Returns an int indicating if CODE was a special code or not */
int handleSpecialCodes(hashArray **pha, int code, int *oldC,
	int *finalK, long long *timeptr)
{
	hashArray *ha = *pha;
	long long time = *timeptr;
	if(code == EMPTYCODE)
	{
		updateLastUse(ha, code, time);
		DIE("Error: received code for EMPTY at time %lld\n", time);
	}
	else if(code == ESCAPECODE)
	{
		if(!ha->e) DIE("Error: received code for ESCAPE without -e flag%s\n","");
		else
		{
			updateLastUse(ha, code, time);
			int c = getBits(CHAR_BIT);
			putchar(c);
			*timeptr = safeTimeIncrement(ha, time); //already incremented once
			int x = haInsertWithLastUse(ha, EMPTYCODE, c, *timeptr);
			if(x == PRUNING_FLAG)
				DIE("Error: pruned at the wrong time in decode%s\n","");
			//If this causes an increment, encode will not send an increase code

			*oldC = EMPTYCODE; //forget previous code after add new char and start over
		}
	}
	else if(code == INCR_NBITSCODE)
	{
		updateLastUse(ha, code, time);
		ha->h->numBits++;
	}
	else if(code == PRUNECODE)
	{
		if(ha->window == 0)
			DIE("Error: received code for PRUNE without -p flag%s\n","");
		else
		{
			updateLastUse(ha, PRUNECODE, time);
		
			int c = getBits(ha->h->numBits);
			time = safeTimeIncrement(ha, time);
			
			if(c == EOF)
				DIE("Error: EOF received after prune code at %lld\n", time);
			if(c != SPACERCODE)
			{
				entry *tempE = haCodeSearch(ha, c);
				if(tempE == NULL) DIE2("Error trying to find %c at time %lld\n", c, time);
				if(tempE->p != EMPTYCODE)
					DIE2("Error: prefix of %c is not EMPTYCODE at time %lld\n", c, time);
				putchar(tempE->k);
			}

			updateLastUse(ha, c, time);

			*pha = prune(ha, time);
			*oldC = 0;
			
			*timeptr = time;
		}
	}
	else if(code == SPACERCODE)
	{
		DIE("Error: received code for SPACER at improper time %lld\n", time);
	}
	else return 0;
	return 1;
}

//Runs the decode part of the LZW algorithm. Takes input from standard input
//generated by encode and prints the decoded text in standard output
//If input is corrupted, decode will either print mixed up text to standard out
//or will DIE during execution
void decode()
{
	hashArray * ha = readParameters();
	if(ha == NULL)
	{
		DIE("Decode: readParameters did not return a hashArray%s\n","");
		return;
	}
	long long time = 0;
	stack *kstack = stackCreate();
	
	int oldC = 0, newC, c, finalK=0;
	bool kwk = false; //indicates whether kwkwk case was found
	while(((newC = c = getBits(ha->h->numBits)) != EOF) && c != EOFCODE)
	{
		time = safeTimeIncrement(ha, time);

		if(handleSpecialCodes(&ha, c, &oldC, &finalK, &time)) continue;
		entry *e;

		if((e = haCodeSearch(ha, c)) == NULL) 
		{
			stackPush(kstack, finalK);
			c = oldC;
			kwk = true;
		}
		else updateLastUse(ha, c, time);
		
		e = haCodeSearch(ha, c);
		if(e == NULL) DIE("Unknown code and not kwkwk at time %lld\n", time);

		while(e->p != 0)
		{
			stackPush(kstack, e->k);
			e = haCodeSearch(ha, e->p);
		}
		
		finalK = e->k;
		putchar(finalK);
		
		int x;
		while((x = stackPop(kstack)) != -1) putchar(x);

		if(oldC != 0)
		{
			int x = haInsert(ha, oldC, finalK, time);
			if(x == PRUNING_FLAG)
				DIE("Error: pruned at wrong time in decode %lld\n", time);
			if(x == NBITS_FLAG)
				DIE("Error: incremented bits at wrong time in decode %lld\n", time);
			if(kwk)
			{
				e = haHashSearch(ha, oldC, finalK);
				if(e == NULL) DIE("Error after kwkwk%s\n","");
				updateLastUse(ha, e->code, time);
				kwk = false;
			}
		}
		oldC = newC;		
	}
	if(c != EOFCODE)
		DIE("Error: ended without receiving EOFCODE at %lld\n", time);
	else
	{
		time = safeTimeIncrement(ha, time);
		updateLastUse(ha, EOFCODE, time);
	}
	freeStack(kstack);
	freeHashArray(ha);
}

/////////////////////////////////////////////////////////////////////
//////////////////////                        ///////////////////////
//////////////////////   DEALING WITH INPUT   ///////////////////////
//////////////////////                        ///////////////////////
/////////////////////////////////////////////////////////////////////

//Prints message about usage and exits program
void usageHelp()
{
  DIE("usage: encode [-m MAXBITS] [-p WINDOW] [-e]%s\n","");
}

//Verifies that the ARGC inputs given in ARGV are properly formatted
//according to the spec. Mallocs and returns a pointer to an inputFlags struct
//Retrieves values from ARGV and sets them in the inputFlags struct
//Determines whether program is called as encode or decode
inputFlags * verifyFlags(int argc, char *argv[])
{
  inputFlags *flags;
  const char *nameStart = strrchr(argv[0], '/')+1;
  char *pEnd;

  if(nameStart == NULL) DIE("no path found%s\n","");
  if(!strcmp(nameStart, "decode"))
  {
    if(argc > 1) DIE("usage: decode%s\n","");
    flags = inputFlagsCreate();
    flags->mode = 'd';
		return flags;
  }
  else if(!strcmp(nameStart, "encode"))
  {
    flags = inputFlagsCreate();
    flags->mode = 'e';
    int i=1;
    while(i<argc)
    {
      if(!strcmp(argv[i],"-e")) flags->e = true;
      else if(!strcmp(argv[i],"-m") )
      {
        if(++i >= argc) usageHelp();
        long maxBits = strtol(argv[i], &pEnd, 10);
        if(strlen(argv[i])!=(pEnd - argv[i]) || maxBits <=0)
          DIE("invalid maxBits %s\n", argv[i]);
        if(maxBits < MINMAXBITS || maxBits > MAXMAXBITS)
          maxBits = DEFAULTMAXBITS;
        flags->maxBits = maxBits;
        flags->m = true;
        //fprintf(stderr,"set maxbits to %ld\n", maxBits);
      }
      else if(!strcmp(argv[i],"-p") )
      {
        if(++i >= argc) usageHelp();
        errno = 0;
        long window = strtol(argv[i], &pEnd, 10);
        if(strlen(argv[i])!=(pEnd - argv[i]) || window <=0)
          DIE("invalid window %s\n", argv[i]);
        
				//code copied from the man page for strtol
        if((errno == ERANGE && (window == LONG_MAX || window == LONG_MIN))
                   || (errno != 0 && window == 0))
        {
					DIE("invalid window %s\n", argv[i]);
        }
        if(pEnd == argv[i])
        {
          DIE("invalid window %s\n", argv[i]);
        }
				//end of code from the man page for strtol
        flags->window = window;
        flags->p = true;
      }
      else usageHelp();
      i++;
    }
  }
  else DIE("Program not called as encode or decode%s\n","");
  return flags;
}

//Verifies input and runs encode or decode
int main(int argc, char *argv[])
{
  inputFlags *flags = verifyFlags(argc, argv);

  hashArray *ha = hashArrayCreate(flags);
	int mode = flags->mode;
	free(flags);

  if(mode == 'e') encode(ha);
  if(mode == 'd')
  {
  	freeHashArray(ha);
  	decode();
  }

  return EXIT_SUCCESS;
}
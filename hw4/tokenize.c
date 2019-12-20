// tokenize.c                                     Stan Eisenstat (10/16/12)
// Source module for Parse

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include "getLine.h"
#include "parse.h"

// Table of special tokens and their lengths and types, ordered so that any
// token that is a prefix of another token appears later in the table
#define ENTRY(x,y) {x, sizeof(x)-1, y}
static struct entry {
	char *text; int length, type;
    } STok[] = {
	ENTRY ("<<",   RED_HERE),
	ENTRY ("<",    RED_IN),
	ENTRY (">>&!", RED_ERR_APP_C),
	ENTRY (">>&",  RED_ERR_APP),
	ENTRY (">>!",  RED_OUT_APP_C),
	ENTRY (">>",   RED_OUT_APP),
	ENTRY (">&!",  RED_ERR_C),
	ENTRY (">&",   RED_ERR),
	ENTRY (">!",   RED_OUT_C),
	ENTRY (">",    RED_OUT),
	ENTRY (";",    SEP_END),
	ENTRY ("&&",   SEP_AND),
	ENTRY ("&",    SEP_BG),
	ENTRY ("||",   SEP_OR),
	ENTRY ("|&",   PIPE_ERR),
	ENTRY ("|",    PIPE),
	ENTRY ("(",    PAR_LEFT),
	ENTRY (")",    PAR_RIGHT),
    };
static int nSTok = sizeof(STok) / sizeof(STok[0]);


// Break string LINE into a headless linked list of typed  tokens and
// returns a pointer to the first token (or NULL if none were found or
// an error was detected).
token *tokenize (char *line)
{
    token head,                 // Dummy head for token list
	 *tail;                 // Pointer to last node in token list
    int inQuote;                // In quoted string?  Value = type
    char *p, *q;
    int i;

    head.next = NULL;
    for (p = line, tail = &head; *p; ) {
	if (isspace(*p)) {                          // Ignore whitespace chars
	    p++;
	    continue;
	} else if (*p == '#') {                     // Ignore comments
	    break;
	}
										  
	tail->next = malloc (sizeof(token));        // Add token to end of list
	tail = tail->next;
	tail->next = NULL;
										  
	for (i = 0; i < nSTok; i++) {               // Check for special token
	    if (!strncmp (p, STok[i].text,
			     STok[i].length))
		break;
	}
						     
	if (i < nSTok) {                            // Special token?
	    tail->type = STok[i].type;
	    tail->text = strdup (STok[i].text);
	    p = p + STok[i].length;
	    continue;
	}
						     
	tail->type = SIMPLE;                        // SIMPLE token
	tail->text = strdup(p);                     // Allocate enough space
	inQuote = 0;
	for (q = tail->text;  *p;  p++)  {
	    if (*p == inQuote) {                    // Matching quote?
		inQuote = 0;                        //   Suppress close quote
						     
	    } else if (inQuote) {                   // Within quotes?
		*q++ = *p;                          //   Copy character
						     
	    } else if (strchr ("'\"", *p)) {        // Start quoted string?
		inQuote = *p;                       //   Suppress start quote
						     
	    } else if (*p == '\\' && p[1]) {        // Escaped character?
		if (p[1] == '\n')
		    *q++ = *p;                      //   Copy \ before newline
		else
		    *q++ = *++p;                    //   Suppress \ otherwise
						     
	    } else if (!strchr(METACHAR,*p)         // Non-whitespace,
		    && !isspace(*p)) {              //     non-metacharacter?
		*q++ = *p;                          //   Copy character
						     
	    } else {
		break;
	    }
	}
	*q = '\0';
	tail->text = realloc (tail->text, q - tail->text + 1);

	if (inQuote) {
	    fprintf (stderr, "Unterminated string\n");
	    freeList (head.next);
	    return NULL;
	}
    }

    return head.next;                                // Return token list
}

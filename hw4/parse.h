// parse.h                                        Stan Eisenstat (10/21/12)
//
// Header file for command line parser used in Parse
//
// Csh version based on recursive descent parse tree

#ifndef PARSE_INCLUDED
#define PARSE_INCLUDED          // parse.h has been #include-d


// A token is
//
// (1) a maximal, contiguous, nonempty sequence of nonwhitespace characters
//     other than the metacharacters <, >, ;, &, |, (, and ) [a SIMPLE token];
//
// (2) a redirection symbol (<, <<, >, >!, >&, >&!, >>, >>!, >>&, >>&!, |, |&);
//
// (3) a command terminator (;, &, &&, or ||);
//
// (4) a left or right parenthesis (used to group commands).


// String containing all metacharacters that terminate SIMPLE tokens
#define METACHAR "<>;&|()"


// A token list is a headless linked list of typed tokens.  All storage is
// allocated by malloc() / realloc().  The token type is specified by the
// symbolic constants defined below.

typedef struct token {          // Struct for each token in linked list
  char *text;                   //   String containing token (if SIMPLE)
  int type;                     //   Corresponding type
  struct token *next;           //   Pointer to next token in linked list
} token;


// Break the string LINE into a headless linked list of typed tokens and
// return a pointer to the first token (or NULL if none were found or an
// error was detected).

token *tokenize (char *line);


// Print out the token list
void dumpList (token *list);


// Free list of tokens LIST
void freeList (token *list);


/////////////////////////////////////////////////////////////////////////////

// Token types used by tokenize() and parse()

enum {

   // Token types used by tokenize() et al.

      SIMPLE,           // Maximal contiguous sequence ... (as above)

      RED_IN,           // <
      RED_HERE,         // <<

      RED_OUT,          // >
      RED_OUT_C,        // >!
      RED_OUT_APP,      // >>
      RED_OUT_APP_C,    // >>!
      RED_ERR,          // >&
      RED_ERR_C,        // >&!
      RED_ERR_APP,      // >>&
      RED_ERR_APP_C,    // >>&!

      PIPE,             // |
      PIPE_ERR,         // |&

      SEP_END,          // ;
      SEP_BG,           // &
      SEP_AND,          // &&
      SEP_OR,           // ||

      PAR_LEFT,         // (
      PAR_RIGHT,        // )

   // Token types used by parse() et al.

      NONE,             // Nontoken: Did not find a token
      ERROR,            // Nontoken: Encountered an error
      SUBCMD            // Nontoken: CMD struct for subcommand
};


// Macros to identify various subclasses of output redirection */
#define ISPIPE(x)    ((x) == PIPE         || (x) == PIPE_ERR)

#define ISAPPEND(x)  ((x) == RED_OUT_APP  || (x) == RED_OUT_APP_C || \
		      (x) == RED_ERR_APP  || (x) == RED_ERR_APP_C)

#define ISERROR(x)   ((x) == RED_ERR      || (x) == RED_ERR_C     || \
		      (x) == RED_ERR_APP  || (x) == RED_ERR_APP_C || \
		      (x) == PIPE_ERR)

#define ISCLOBBER(x) ((x) == RED_OUT_C    || (x) == RED_OUT_APP_C || \
		      (x) == RED_ERR_C    || (x) == RED_ERR_APP_C)


/////////////////////////////////////////////////////////////////////////////


// The syntax for a command is
//
//   <stage>    = <simple> / (<command>)
//   <pipeline> = <stage> / <stage> | <pipeline>
//   <and-or>   = <pipeline> / <pipeline> && <and-or> / <pipeline> || <and-or>
//   <command>  = <and-or> / <and-or> ; <command> / <and-or> ;
//                         / <and-or> & <command> / <and-or> &
//
// where a <simple> is a single command with arguments and I/O redirection but
// no |, &, ;, &&, ||, (, or ).  Note that I/O redirection is associated with a
// <stage> (i.e., a <simple> or subcommand), but not with a <pipeline> (input/
// output redirection for the first/last stage is associated with the stage,
// not the pipeline).
//
// A command is represented by a tree of CMD structs containing its <simple>
// commands and the "operators" |, |&, &&, ||, ;, &, and SUBCMD.  The tree
// corresponds to how the command is parsed by a recursive descent parser
// (see https://en.wikipedia.org/wiki/Recursive_descent_parser) for the
// grammar above.
//
// The tree for a <simple> is a single struct of type SIMPLE that specifies its
// arguments (argc, argv[]); and whether and where to redirect its standard
// input (fromType, fromFile) or its standard output (toType, toFile).  The
// left and right children are NULL.
//
// The tree for a <stage> is either the tree for a <simple> or a CMD struct
// of type SUBCMD (which may have redirection) whose left child is the tree
// representing a <command> and whose right child is NULL.
//
// The tree for a <pipeline> is either the tree for a <stage> or a CMD struct
// of type PIPE or PIPE_ERR whose left child is the tree representing the
// <stage> and whose right child is the tree representing the rest of the
// <pipeline>.
//
// The tree for an <and-or> is either the tree for a <pipeline> or a CMD
// struct of type && (= SEP_AND) or || (= SEP_OR) whose left child is the tree
// representing the <pipeline> and whose right child is the tree representing
// the <and-or>.
//
// The tree for a <command> is either the tree for an <and-or> or a CMD
// struct of type ; (= SEP_END) or & (= SEP_BG) whose left child is the tree
// representing the <and-or> and whose right child is either NULL or the tree
// representing the <command>.
//
// Examples (where A, B, C, D, and E are <simple>):
//
//                              Recursive Descent                             //
//                                                                            //
//   A                          A                                             //
//                                                                            //
//   < A B | C | D |& E > F       PIPE                                        //
//                               /    \                                       //
//                            <A B     PIPE                                   //
//                                    /    \                                  //
//                                   C    PIPE_ERR                            //
//                                         /    \                             //
//                                        D      E >F                         //
//                                                                            //
//   A && B || C && D             &&                                          //
//                               /  \                                         //
//                              A    ||                                       //
//                                  /  \                                      //
//                                 B    &&                                    //
//                                     /  \                                   //
//                                    C    D                                  //
//                                                                            //
//   A ; B & C ; D || E           ;                                           //
//                               / \                                          //
//                              A   &                                         //
//                                 / \                                        //
//                                B   ;                                       //
//                                   / \                                      //
//                                  C   ||                                    //
//                                     /  \                                   //
//                                    D    E                                  //
//                                                                            //
//   (A ; B &) | (C || D) && E                &&                              //
//                                           /  \                             //
//                                       PIPE    E                            //
//                                     /      \                               //
//                                  SUB        SUB                            //
//                                 /          /                               //
//                                ;         ||                                //
//                               / \       /  \                               //
//                              A   &     C    D                              //
//                                 /                                          //
//                                B                                           //

typedef struct cmd {
  int type;             // Node type (SIMPLE, PIPE, PIPE_ERR, SUBCMD,
			//   SEP_AND, SEP_OR, SEP_END, SEP_BG, or NONE)

  int argc;             // Number of command-line arguments
  char **argv;          // Null-terminated argument vector

  int fromType;         // Redirect stdin?
			//  (NONE (default), RED_IN, or RED_HERE)
  char *fromFile;       // File to redirect stdin. contents of here
			//   document, or NULL (default)

  int toType;           // Redirect stdout?
			//  (NONE (default), RED_OUT, RED_OUT_C, RED_OUT_APP,
			//   RED_OUT_APP_C, RED_ERR, RED_ERR_C, RED_ERR_APP,
			//   or RED_ERR_APP_C)
  char *toFile;         // File to redirect stdout or NULL (default)

  struct cmd *left;     // Left subtree or NULL (default)
  struct cmd *right;    // Right subtree or NULL (default)
} CMD;

									      
// Allocate, initialize, and return a pointer to an empty command structure
CMD *mallocCMD (void);


// Print out the command data structure CMD
void dumpCMD (CMD *exec, int level);


// Free the command structure CMD
void freeCMD (CMD *cmd);


// Print tree of CMD structs in in-order starting at LEVEL
void dumpTree (CMD *exec, int level);


// Parse a token list into a command structure and return a pointer to
// that structure (NULL if errors found).
CMD *parse (token *tok);

#endif

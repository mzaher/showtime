/*
 *  GL Widgets, model loader, preprocessor
 *  Copyright (C) 2008 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include "glw_model.h"
#include <stdio.h>

LIST_HEAD(macro_list, macro);
TAILQ_HEAD(macro_arg_queue, macro_arg);

typedef struct macro_arg {
  token_t *first, *last;
  char *name;
  TAILQ_ENTRY(macro_arg) link;
} macro_arg_t;


typedef struct macro {
  token_t *body;
  char *name;
  struct macro_arg_queue args;

  LIST_ENTRY(macro) link;
} macro_t;


/**
 *
 */
static void
macro_destroy(macro_t *m)
{
  macro_arg_t *ma;

  while((ma = TAILQ_FIRST(&m->args)) != NULL) {
    TAILQ_REMOVE(&m->args, ma, link);
    free(ma->name);
    free(ma);
  }

  LIST_REMOVE(m, link);
  glw_model_free_chain(m->body);
  free(m->name);
  free(m);
}

/**
 *
 */
static void
macro_add_arg(macro_t *m, const char *name)
{
  macro_arg_t *ma = calloc(1, sizeof(macro_arg_t));
  ma->name = strdup(name);
  TAILQ_INSERT_TAIL(&m->args, ma, link);
}



#define consumetoken() assert(p != t); p->next = t->next; glw_model_token_free(t); t = p->next


/**
 *
 */
static int
glw_model_preproc0(glw_root_t *gr, token_t *p, errorinfo_t *ei,
		   struct macro_list *ml)
{
  token_t *t, *n, *x, *a, *b, *c, *d, *e;
  macro_t *m;
  macro_arg_t *ma;
  int balance = 0;

  while(1) {
    t = p->next;
    if(t->type == TOKEN_END)
      return 0;

    if(t->type == TOKEN_IDENTIFIER) {
      n = t->next;
      if(n->type == TOKEN_LEFT_PARENTHESIS) {
	LIST_FOREACH(m, ml, link)
	  if(!strcmp(m->name, t->t_string))
	    break;

	if(m != NULL) {
	  consumetoken(); /* identifier */
	  consumetoken(); /* left parenthesis */

	  x = p;

	  c = p->next; /* Pointer to argument list, used for freeing later */

	  TAILQ_FOREACH(ma, &m->args, link) {
	    ma->first = p->next;

	    while(1) {
	      t = p->next;
	      if(t->type == TOKEN_END)
		return glw_model_seterr(ei, t, "Unexpected end of input in "
					"macro invokation");
	      
	      if(t->type == TOKEN_RIGHT_PARENTHESIS && balance == 0) {
		if(TAILQ_NEXT(ma, link) != NULL)
		  return glw_model_seterr(ei, t, 
					  "Too few arguments to macro %s",
					  m->name);
		break;
	      }

	      if(t->type == TOKEN_BLOCK_CLOSE ||
		 t->type == TOKEN_RIGHT_PARENTHESIS ||
		 t->type == TOKEN_RIGHT_BRACKET) {
		balance--;
	      } else if(t->type == TOKEN_BLOCK_OPEN ||
			t->type == TOKEN_LEFT_PARENTHESIS ||
			t->type == TOKEN_LEFT_BRACKET) {
		balance++;

	      } else if(t->type == TOKEN_SEPARATOR && balance == 0)
		break;
	      p = p->next;
	    }

	    ma->last = p;

	    if(t->type == TOKEN_RIGHT_PARENTHESIS)
	      break;
	    p = p->next;
	  }

	  if(t->type != TOKEN_RIGHT_PARENTHESIS)
	    return glw_model_seterr(ei, t, 
				    "Too many arguments to macro %s",
				    m->name);

	  d = t->next;
	  t->next = NULL;

	  p = x;
	  b = NULL;
	  p->next = d;

	  for(a = m->body; a != NULL; a = a->next) {

	    if(a->tmp != NULL) {
	      ma = a->tmp;
	      e = ma->first;

	      while(1) {
		b = glw_model_token_copy(e);
		p->next = b;
		p = b;
		
		if(e == ma->last)
		  break;
		e = e->next;
	      }

	    } else {
	      b = glw_model_token_copy(a);
	      p->next = b;
	      p = b;
	    }
	  }

	  if(b != NULL)
	    b->next = d;

	  p = x;

	  glw_model_free_chain(c);

	  continue;

	}
      }
    }






    if(t->type != TOKEN_HASH) {
      p = p->next;
      continue;
    }

    consumetoken();

    if(t->type == TOKEN_IDENTIFIER) {

      /**
       * Include another file
       */
      if(!strcmp(t->t_string, "include")) {
	consumetoken();

	if(t->type != TOKEN_STRING) 
	  return glw_model_seterr(ei, t, "Invalid filename after include");

	x = t->next;
	if((n = glw_model_load1(gr, t->t_string, ei, t)) == NULL)
	  return -1;

	n->next = x;
	consumetoken();
	continue;
      }


      /**
       * Define a macro
       */
      if(!strcmp(t->t_string, "define")) {
	consumetoken();
	
	if(t->type != TOKEN_IDENTIFIER)
	  return glw_model_seterr(ei, t, "Invalid macro name");

	m = calloc(1, sizeof(macro_t));
	TAILQ_INIT(&m->args);
	LIST_INSERT_HEAD(ml, m, link);

	m->name = strdup(t->t_string);
	consumetoken();

	if(t->type != TOKEN_LEFT_PARENTHESIS)
	  return glw_model_seterr(ei, t, "Expected '(' after macro name");
	consumetoken();

	if(t->type != TOKEN_RIGHT_PARENTHESIS) {

	  while(1) {
	    if(t->type != TOKEN_IDENTIFIER)
	      return glw_model_seterr(ei, t, "Expected macro argument");
	  
	    macro_add_arg(m, t->t_string);
	    consumetoken();
	  
	    if(t->type == TOKEN_RIGHT_PARENTHESIS) {
	      consumetoken();
	      break;
	    }

	    if(t->type != TOKEN_SEPARATOR)
	      return glw_model_seterr(ei, t, 
				      "Expected ',' or ')' "
				      "after macro argument");
	    consumetoken();
	  }
	} else {
	  consumetoken();
	}

	if(t->type != TOKEN_BLOCK_OPEN)
	  return glw_model_seterr(ei, t, "Expected '{' after macro header");
	consumetoken();

	x = p;

	while(1) {
	  t = p->next;
	  if(t->type == TOKEN_END)
	    return glw_model_seterr(ei, t, "Unexpected end of input in "
				    "macro definition");
	  
	  if(t->type == TOKEN_BLOCK_CLOSE) {
	    if(balance == 0) {
	      consumetoken();
	      break;
	    }
	    balance--;
	  } else if(t->type == TOKEN_BLOCK_OPEN) {
	    balance++;

	  } else if(t->type == TOKEN_IDENTIFIER) {

	    TAILQ_FOREACH(ma, &m->args, link) {
	      if(!strcmp(ma->name, t->t_string)) {
		t->tmp = ma;
		break;
	      }
	    }
	  }
	  p = p->next;
	}

	p->next = NULL;
	m->body = x->next;

	x->next = t;
	p = x;
	continue;
      }

      


    }
    return glw_model_seterr(ei, t, "Invalid preprocessor directive");
  }
}

/**
 *
 */
int
glw_model_preproc(glw_root_t *gr, token_t *p, errorinfo_t *ei)
{
  struct macro_list ml;
  macro_t *m;
  int r;
  
  LIST_INIT(&ml);
  
  r = glw_model_preproc0(gr, p, ei, &ml);
  
  while((m = LIST_FIRST(&ml)) != NULL)
    macro_destroy(m);
  
  return r;
}
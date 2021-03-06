/*
 *  GL Widgets
 *  Copyright (C) 2010 Andreas Öman
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

#include "misc/string.h"
#include "event.h"
#include "glw.h"
#include <libavutil/common.h>

#define KI_BUF_LEN 64

typedef struct glw_keyintercept {
  glw_t w;

  int buf[KI_BUF_LEN];
  int buflen;

  prop_sub_t *sub;
  prop_t *prop;

} glw_keyintercept_t;


/**
 *
 */
static void
updatestr(glw_keyintercept_t *ki)
{
  char str[KI_BUF_LEN * 5 + 1];
  char *q;
  int i, c;

  q = str;
  for(i = 0; i < ki->buflen; i++) {
    uint8_t tmp;
    c = ki->buf[i];
    PUT_UTF8(c, tmp, if (q - str < KI_BUF_LEN * 5 ) *q++ = tmp;)
  }
  *q = 0;

  if(ki->prop != NULL)
    prop_set_string_ex(ki->prop, ki->sub, str, PROP_STR_UTF8);
}

/**
 *
 */
static int
ki_handle_event(glw_keyintercept_t *ki, event_t *e)
{
  if(event_is_action(e, ACTION_BS)) {
    if(ki->buflen == 0)
      return 0;
    ki->buflen--;
    updatestr(ki);
    return 1;
  }

  if(event_is_type(e, EVENT_UNICODE)) {
    event_int_t *ei = (event_int_t *)e;
    int c = ei->val;
    if(c == 32 && ki->buflen == 0)
      return 0; // space as first char is not something we trig on

    if(ki->buflen == KI_BUF_LEN - 1)
      return 1;
    
    ki->buf[ki->buflen] = c;
    ki->buflen++;
    updatestr(ki);
    return 1;
  }
  return 0;
}

/**
 *
 */
static void
ki_unbind(glw_keyintercept_t *ki)
{
  if(ki->sub != NULL) {
    prop_unsubscribe(ki->sub);
    ki->sub = NULL;
  }

  if(ki->prop != NULL) {
    prop_ref_dec(ki->prop);
    ki->prop = NULL;
  }
}


/**
 *
 */
static int
glw_keyintercept_callback(glw_t *w, void *opaque, 
			  glw_signal_t signal, void *extra)
{
  glw_t *c;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_DESTROY:
    ki_unbind((glw_keyintercept_t *)w);
    break;
  case GLW_SIGNAL_LAYOUT:
    c = TAILQ_FIRST(&w->glw_childs);
    if(c != NULL)
      glw_layout0(c, extra);
    break;
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    glw_copy_constraints(w, extra);
    return 1;

  case GLW_SIGNAL_EVENT_BUBBLE:
    if(w->glw_flags2 & GLW2_ENABLED)
      return ki_handle_event((glw_keyintercept_t *)w, extra);
    else
      return 0;
  }
  return 0;
}

/**
 *
 */
static void
glw_keyintercept_render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;

  if((c = TAILQ_FIRST(&w->glw_childs)) != NULL)
    glw_render0(c, rc);
}


/**
 *
 */
static void
prop_callback(void *opaque, prop_event_t event, ...)
{
  glw_keyintercept_t *ki = opaque;
  const char *str;
  prop_t *p;
  int c;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_SET_VOID:
    ki->buflen = 0;
    str = NULL;
    p = va_arg(ap, prop_t *);
    break;

  case PROP_SET_RSTRING:
    str = rstr_get(va_arg(ap, const rstr_t *));

    ki->buflen = 0;
    while((c = utf8_get(&str)) != 0 && ki->buflen < 64)
      ki->buf[ki->buflen++] = c;
    p = va_arg(ap, prop_t *);
    break;

  default:
    return;
  }

  prop_ref_dec(ki->prop);
  ki->prop = prop_ref_inc(p);
}


/**
 *
 */
static void 
bind_to_property(glw_t *w, prop_t *p, const char **pname,
		 prop_t *view, prop_t *args, prop_t *clone)
{
  glw_keyintercept_t *ki = (glw_keyintercept_t *)w;
  ki_unbind(ki);

  ki->sub = 
    prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		   PROP_TAG_NAME_VECTOR, pname, 
		   PROP_TAG_CALLBACK, prop_callback, ki, 
		   PROP_TAG_COURIER, w->glw_root->gr_courier,
		   PROP_TAG_NAMED_ROOT, p, "self",
		   PROP_TAG_NAMED_ROOT, view, "view",
		   PROP_TAG_NAMED_ROOT, args, "args",
		   PROP_TAG_NAMED_ROOT, clone, "clone",
		   PROP_TAG_ROOT, w->glw_root->gr_uii.uii_prop,
		   NULL);
}


/**
 *
 */
static glw_class_t glw_keyintercept = {
  .gc_name = "keyintercept",
  .gc_instance_size = sizeof(glw_keyintercept_t),
  .gc_render = glw_keyintercept_render,
  .gc_signal_handler = glw_keyintercept_callback,
  .gc_bind_to_property = bind_to_property,
};

GLW_REGISTER_CLASS(glw_keyintercept);

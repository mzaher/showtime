/*
 *  GL Widgets, common stuff
 *  Copyright (C) 2007 Andreas �man
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

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <assert.h>

#include <arch/threads.h>

#include "glw.h"
#include "glw_container.h"
#include "glw_stack.h"
#include "glw_text_bitmap.h"
#include "glw_image.h"
#include "glw_array.h"
#include "glw_cursor.h"
#include "glw_rotator.h"
#include "glw_model.h"
#include "glw_list.h"
#include "glw_deck.h"
#include "glw_expander.h"
#include "glw_slideshow.h"
#include "glw_mirror.h"
#include "glw_animator.h"
#include "glw_fx_texrot.h"
#include "glw_event.h"
#include "glw_video.h"
#include "glw_slider.h"
#include "glw_layer.h"

static const size_t glw_class_to_size[] = {
  [GLW_DUMMY] = sizeof(glw_t),
  [GLW_MODEL] = sizeof(glw_t),
  [GLW_CONTAINER_X] = sizeof(glw_t),
  [GLW_CONTAINER_Y] = sizeof(glw_t),
  [GLW_CONTAINER_Z] = sizeof(glw_t),
  [GLW_STACK_X] = sizeof(glw_t),
  [GLW_STACK_Y] = sizeof(glw_t),
  [GLW_IMAGE]  = sizeof(glw_image_t),
  [GLW_LABEL]  = sizeof(glw_text_bitmap_t),
  [GLW_TEXT]  = sizeof(glw_text_bitmap_t),
  [GLW_INTEGER]  = sizeof(glw_text_bitmap_t),
  [GLW_ROTATOR] = sizeof(glw_t),
  //  [GLW_ARRAY] = sizeof(glw_array_t),
  [GLW_LIST_X] = sizeof(glw_list_t),
  [GLW_LIST_Y] = sizeof(glw_list_t),
  [GLW_DECK] = sizeof(glw_deck_t),
  [GLW_EXPANDER] = sizeof(glw_t),
  //  [GLW_SLIDESHOW] = sizeof(glw_slideshow_t),
  [GLW_CURSOR] = sizeof(glw_cursor_t),
  [GLW_MIRROR] = sizeof(glw_t),
  [GLW_ANIMATOR] = sizeof(glw_animator_t),
  [GLW_FX_TEXROT] = sizeof(glw_fx_texrot_t),
  [GLW_VIDEO] = sizeof(glw_video_t),
  [GLW_SLIDER_X] = sizeof(glw_slider_t),
  [GLW_SLIDER_Y] = sizeof(glw_slider_t),
  [GLW_LAYER] = sizeof(glw_t),
};


static void glw_focus_init_widget(glw_t *w);
static void glw_focus_leave(glw_t *w);

/*
 *
 */
void
glw_lock(glw_root_t *gr)
{
  hts_mutex_lock(&gr->gr_mutex);
}

/*
 *
 */
void
glw_unlock(glw_root_t *gr)
{
  hts_mutex_unlock(&gr->gr_mutex);
}


/*
 *
 */
void
glw_cond_wait(glw_root_t *gr, hts_cond_t *c)
{
  hts_cond_wait(c, &gr->gr_mutex);
}


/**
 *
 */
int
glw_init(glw_root_t *gr, float fontsize)
{
  hts_mutex_init(&gr->gr_mutex);
  gr->gr_courier = prop_courier_create(&gr->gr_mutex);

  if(glw_text_bitmap_init(gr, fontsize)) {
    free(gr);
    return -1;
  }

  TAILQ_INIT(&gr->gr_destroyer_queue);
  glw_tex_init(gr);

  gr->gr_frameduration = 1000000 / 60;

  //  glw_check_system_features(gr);
  return 0;
}


/*
 *
 */
int
glw_attrib_set0(glw_t *w, int init, va_list ap)
{
  glw_attribute_t attrib;
  glw_t *p, *b;
  void *v, *o;
  int pri, a, r = 0;
  glw_root_t *gr = w->glw_root;

  va_list apx;

  va_copy(apx, ap);

  do {
    attrib = va_arg(ap, int);

    switch(attrib) {

    case GLW_ATTRIB_SIGNAL_HANDLER:
      v   = va_arg(ap, void *);
      o   = va_arg(ap, void *);
      pri = va_arg(ap, int);

      if(pri == -1)
	glw_signal_handler_unregister(w, v, o);
      else
	glw_signal_handler_register(w, v, o, pri);
      break;

    case GLW_ATTRIB_PARENT:
    case GLW_ATTRIB_PARENT_HEAD:
    case GLW_ATTRIB_PARENT_BEFORE:
      if(w->glw_parent != NULL) {

	glw_signal0(w->glw_parent, GLW_SIGNAL_CHILD_DESTROYED, w);

	if(w->glw_parent->glw_selected == w)
	  w->glw_parent->glw_selected = TAILQ_NEXT(w, glw_parent_link);

	if(w->glw_flags & GLW_RENDER_LINKED) {
	  w->glw_flags &= ~GLW_RENDER_LINKED;
	  TAILQ_REMOVE(&w->glw_parent->glw_render_list, w, glw_render_link);
	}

	TAILQ_REMOVE(&w->glw_parent->glw_childs, w, glw_parent_link);

      }
      p = va_arg(ap, void *);

      w->glw_parent = p;
      if(p != NULL) {
	if(attrib == GLW_ATTRIB_PARENT_BEFORE) {
	  b = va_arg(ap, void *);
	  if(b == NULL) {
	    TAILQ_INSERT_TAIL(&w->glw_parent->glw_childs, w, glw_parent_link);
	  } else {
	    TAILQ_INSERT_BEFORE(b, w, glw_parent_link);
	  }

	} else if(attrib == GLW_ATTRIB_PARENT_HEAD) {
	  TAILQ_INSERT_HEAD(&w->glw_parent->glw_childs, w, glw_parent_link);
	} else {
	  TAILQ_INSERT_TAIL(&w->glw_parent->glw_childs, w, glw_parent_link);
	}
	glw_signal0(p, GLW_SIGNAL_CHILD_CREATED, w);
      }
      break;

    case GLW_ATTRIB_WEIGHT:
      w->glw_conf_weight = va_arg(ap, double);
      break;

    case GLW_ATTRIB_ASPECT:
      w->glw_aspect = va_arg(ap, double);
      break;

    case GLW_ATTRIB_ID:
      v = va_arg(ap, char *);
      free((void *)w->glw_id);
      w->glw_id = v ? strdup(v) : NULL;
      break;

    case GLW_ATTRIB_ALPHA:
      w->glw_alpha = va_arg(ap, double);
      break;

    case GLW_ATTRIB_EXTRA:
      w->glw_extra = va_arg(ap, double);
      break;

    case GLW_ATTRIB_ALIGNMENT:
      w->glw_alignment = va_arg(ap, int);
      break;

    case GLW_ATTRIB_SET_FLAGS:
      a = va_arg(ap, int);

      a &= ~w->glw_flags; // Mask out already set flags

      if(a & GLW_EVERY_FRAME)
	LIST_INSERT_HEAD(&gr->gr_every_frame_list, w, glw_every_frame_link);
  
      w->glw_flags |= a;

      if(a & GLW_FOCUSABLE)
	glw_focus_init_widget(w);
      break;

    case GLW_ATTRIB_CLR_FLAGS:
      a = va_arg(ap, int);

      a &= w->glw_flags; // Mask out already cleared flags

      if(a & GLW_EVERY_FRAME)
	LIST_REMOVE(w, glw_every_frame_link);

      if(a & GLW_FOCUSABLE)
	glw_focus_leave(w);

      w->glw_flags &= ~a;
      break;

    case GLW_ATTRIB_ANGLE:
      w->glw_extra = va_arg(ap, double);
      break;

    case GLW_ATTRIB_DISPLACEMENT:
      w->glw_displacement.x = va_arg(ap, double);
      w->glw_displacement.y = va_arg(ap, double);
      w->glw_displacement.z = va_arg(ap, double);
      break;

    case GLW_ATTRIB_TIME:
      w->glw_time = va_arg(ap, double);
      break;

    case GLW_ATTRIB_EXPAND:
      w->glw_exp_req = va_arg(ap, double);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

  /* Per-class attributes are parsed by class-specific parser,
     this function also servers as a per-class constructor */

  switch(w->glw_class) {

  case GLW_CONTAINER_X:
  case GLW_CONTAINER_Y:
  case GLW_CONTAINER_Z:
    glw_container_ctor(w, init, apx);
    break;


  case GLW_STACK_X:
  case GLW_STACK_Y:
    glw_stack_ctor(w, init, apx);
    break;

  case GLW_IMAGE:
    glw_image_ctor(w, init, apx);
    break;

  case GLW_LABEL:
  case GLW_TEXT:
  case GLW_INTEGER:
    glw_text_bitmap_ctor(w, init, apx);
    break;
#if 0
  case GLW_ARRAY:
    glw_array_ctor(w, init, apx);
    break;
#endif
  case GLW_ROTATOR:
    glw_rotator_ctor(w, init, apx);
    break;

  case GLW_LIST_X:
  case GLW_LIST_Y:
    glw_list_ctor(w, init, apx);
    break;

  case GLW_DECK:
    glw_deck_ctor(w, init, apx);
    break;

  case GLW_EXPANDER:
    glw_expander_ctor(w, init, apx);
    break;
#if 0
  case GLW_SLIDESHOW:
    glw_slideshow_ctor(w, init, apx);
    break;
#endif
  case GLW_DUMMY:
    break;

  case GLW_CURSOR:
    glw_cursor_ctor(w, init, apx);
    break;

  case GLW_MIRROR:
    //    glw_mirror_ctor(w, init, apx);
    break;

  case GLW_MODEL:
    glw_model_ctor(w, init, apx);
    break;

  case GLW_ANIMATOR:
    glw_animator_ctor(w, init, apx);
    break;

  case GLW_FX_TEXROT:
    glw_fx_texrot_ctor(w, init, apx);
    break;

  case GLW_VIDEO:
    glw_video_ctor(w, init, apx);
    break;

  case GLW_SLIDER_X:
  case GLW_SLIDER_Y:
    glw_slider_ctor(w, init, apx);
    break;

  case GLW_LAYER:
    glw_layer_ctor(w, init, apx);
    break;

  }

  va_end(apx);
  return r;
}

/**
 *
 */
glw_t *
glw_create0(glw_root_t *gr, glw_class_t class, va_list ap)
{
  size_t size; 
  glw_t *w; 

  /* Common initializers */

  size = glw_class_to_size[class];
  w = calloc(1, size);
  w->glw_root = gr;
  w->glw_class = class;
  w->glw_alpha = 1.0f;
  w->glw_conf_weight = 1.0f;
  w->glw_time = 1.0f;

  /* XXX: Not good */
  switch(w->glw_class) {
  default:
    break;
  case GLW_LABEL:
  case GLW_TEXT:
  case GLW_INTEGER:
    w->glw_alignment = GLW_ALIGN_LEFT;
    break;
  case GLW_IMAGE:
    w->glw_alignment = GLW_ALIGN_CENTER;
    break;
  case GLW_STACK_X:
    w->glw_alignment = GLW_ALIGN_LEFT;
    break;
  case GLW_STACK_Y:
    w->glw_alignment = GLW_ALIGN_TOP;
    break;
  }

  LIST_INSERT_HEAD(&gr->gr_active_dummy_list, w, glw_active_link);

  TAILQ_INIT(&w->glw_childs);
  TAILQ_INIT(&w->glw_render_list);

  /* Parse arguments */
  
  if(glw_attrib_set0(w, 1, ap) < 0) {
    glw_destroy0(w);
    return NULL;
  }

  return w;
}

/*
 *
 */

glw_t *
glw_create_i(glw_root_t *gr, glw_class_t class, ...)
{
  glw_t *w; 
  va_list ap;

  va_start(ap, class);
  w = glw_create0(gr, class, ap);
  va_end(ap);
  return w;
}


/**
 *
 */
void
glw_set_i(glw_t *w, ...)
{
  va_list ap;

  va_start(ap, w);
  glw_attrib_set0(w, 0, ap);
  va_end(ap);
}


/**
 *
 */
static void
glw_signal_handler_clean(glw_t *w)
{
  glw_signal_handler_t *gsh;
  while((gsh = LIST_FIRST(&w->glw_signal_handlers)) != NULL) {
    LIST_REMOVE(gsh, gsh_link);
    free(gsh);
  }
}



/**
 *
 */
void
glw_reaper0(glw_root_t *gr)
{
  glw_t *w;

  glw_cursor_layout_frame(gr);

  LIST_FOREACH(w, &gr->gr_every_frame_list, glw_every_frame_link)
    glw_signal0(w, GLW_SIGNAL_NEW_FRAME, NULL);

  while((w = LIST_FIRST(&gr->gr_active_flush_list)) != NULL) {
    LIST_REMOVE(w, glw_active_link);
    LIST_INSERT_HEAD(&gr->gr_active_dummy_list, w, glw_active_link);
    glw_signal0(w, GLW_SIGNAL_INACTIVE, NULL);
  }

  LIST_MOVE(&gr->gr_active_flush_list, &gr->gr_active_list, glw_active_link);
  LIST_INIT(&gr->gr_active_list);

  glw_tex_purge(gr);

  glw_tex_autoflush(gr);

  while((w = TAILQ_FIRST(&gr->gr_destroyer_queue)) != NULL) {
    TAILQ_REMOVE(&gr->gr_destroyer_queue, w, glw_parent_link);

    glw_signal0(w, GLW_SIGNAL_DTOR, NULL);

    w->glw_flags |= GLW_DESTROYED;
    glw_signal_handler_clean(w);

    if(w->glw_refcnt == 0)
      free(w);
  }
}

/*
 *
 */
void
glw_deref0(glw_t *w)
{
  if(w->glw_refcnt == 1)
    free(w);
  else
    w->glw_refcnt--;
}

/*
 *
 */
void
glw_destroy0(glw_t *w)
{
  glw_t *c, *p;
  glw_root_t *gr = w->glw_root;
  glw_event_map_t *gem;

  if(gr->gr_pointer_grab == w)
    gr->gr_pointer_grab = NULL;

  glw_prop_subscription_destroy_list(&w->glw_prop_subscriptions);

  while((gem = LIST_FIRST(&w->glw_event_maps)) != NULL) {
    LIST_REMOVE(gem, gem_link);
    gem->gem_dtor(gem);
  }

  free(w->glw_matrix);
  w->glw_matrix = NULL;
  
  if(w->glw_flags & GLW_EVERY_FRAME)
    LIST_REMOVE(w, glw_every_frame_link);

  if(w->glw_flags & GLW_RENDER_LINKED)
    TAILQ_REMOVE(&w->glw_parent->glw_childs, w, glw_render_link);

  LIST_REMOVE(w, glw_active_link);

  while((c = TAILQ_FIRST(&w->glw_childs)) != NULL)
    glw_destroy0(c);

  glw_signal0(w, GLW_SIGNAL_DESTROY, NULL);

  if((p = w->glw_parent) != NULL) {
    /* Some classes needs to do some stuff is a child is destroyed */
    glw_signal0(p, GLW_SIGNAL_CHILD_DESTROYED, w);

    glw_focus_leave(w);

    if(p->glw_selected == w)
      p->glw_selected = TAILQ_NEXT(w, glw_parent_link);

    TAILQ_REMOVE(&p->glw_childs, w, glw_parent_link);
  }

  free((void *)w->glw_id);

  TAILQ_INSERT_TAIL(&gr->gr_destroyer_queue, w, glw_parent_link);

  glw_model_free_chain(w->glw_dynamic_expressions);
}


/*
 *
 */
void
glw_set_active0(glw_t *w)
{
  glw_root_t *gr = w->glw_root;
  LIST_REMOVE(w, glw_active_link);
  LIST_INSERT_HEAD(&gr->gr_active_list, w, glw_active_link);
}

/*
 *
 */
void
glw_signal_handler_register(glw_t *w, glw_callback_t *func, void *opaque, 
			    int pri)
{
  glw_signal_handler_t *gsh, *p = NULL;

  LIST_FOREACH(gsh, &w->glw_signal_handlers, gsh_link) {
    if(gsh->gsh_func == func) {
      gsh->gsh_opaque = opaque;
      return;
    }
    if(gsh->gsh_pri < pri)
      p = gsh;
  } 

  gsh = malloc(sizeof(glw_signal_handler_t));
  gsh->gsh_func   = func;
  gsh->gsh_opaque = opaque;
  gsh->gsh_pri    = pri;

  if(p == NULL) {
    LIST_INSERT_HEAD(&w->glw_signal_handlers, gsh, gsh_link);
  } else {
    LIST_INSERT_AFTER(p, gsh, gsh_link);
  }
}

/*
 *
 */
void
glw_signal_handler_unregister(glw_t *w, glw_callback_t *func, void *opaque)
{
  glw_signal_handler_t *gsh;

  LIST_FOREACH(gsh, &w->glw_signal_handlers, gsh_link)
    if(gsh->gsh_func == func && gsh->gsh_opaque == opaque)
      break;
  
  if(gsh != NULL) {
    LIST_REMOVE(gsh, gsh_link);
    free(gsh);
  }
}

/**
 *
 */
glw_t *
glw_get_prev_n(glw_t *c, int count)
{
  glw_t *t = c;
  int i;
  c = NULL;
  for(i = 0; i < count; i++) {
    if((t = TAILQ_PREV(t, glw_queue, glw_parent_link)) == NULL)
      break;
    c = t;
  }
  return c;
}


/**
 *
 */
glw_t *
glw_get_next_n(glw_t *c, int count)
{
  glw_t *t = c;
  int i;

  c = NULL;

  for(i = 0; i < count; i++) {
    if((t = TAILQ_NEXT(t, glw_parent_link)) == NULL)
      break;
    c = t;
  }
  return c;
}


/**
 *
 */
glw_t *
glw_get_prev_n_all(glw_t *c, int count)
{
  int i;
  for(i = 0; i < count; i++) {
    if((c = TAILQ_PREV(c, glw_queue, glw_parent_link)) == NULL)
      break;
  }
  return c;
}


/**
 *
 */
glw_t *
glw_get_next_n_all(glw_t *c, int count)
{
  int i;
  for(i = 0; i < count; i++) {
    if((c = TAILQ_NEXT(c, glw_parent_link)) == NULL)
      break;
  }
  return c;
}


/**
 *
 */

static LIST_HEAD(, glw_gf_ctrl) ggcs;

void
glw_gf_register(glw_gf_ctrl_t *ggc)
{
  LIST_INSERT_HEAD(&ggcs, ggc, link);
}

void
glw_gf_unregister(glw_gf_ctrl_t *ggc)
{
  LIST_REMOVE(ggc, link);
}

void
glw_gf_do(void)
{
  glw_gf_ctrl_t *ggc;
  LIST_FOREACH(ggc, &ggcs, link)
    ggc->flush(ggc->opaque);
}




/**
 *
 */
void
glw_flush0(glw_root_t *gr)
{
  glw_gf_do();
  glw_tex_flush_all(gr);
  glw_text_flush(gr);
}


/**
 *
 */
void
glw_detach0(glw_t *w)
{
  glw_t *p;
  glw_signal_handler_t *gsh;

  p = w->glw_parent;
  if(p != NULL) {

    LIST_FOREACH(gsh, &p->glw_signal_handlers, gsh_link)
      if(gsh->gsh_func(p, gsh->gsh_opaque, GLW_SIGNAL_DETACH_CHILD, w))
	break;

    if(gsh == NULL)
      /* Parent does not support detach, destroy child instead */
      glw_destroy0(w);
  }
}



/**
 *
 */
int
glw_is_focused(glw_t *w)
{
  return w->glw_root->gr_current_focus == w;
}

/**
 *
 */
static int
glw_path_in_focus(glw_t *w)
{
  while(w->glw_parent != NULL) {
    if(w->glw_parent->glw_focused != w)
      return 0;
    w = w->glw_parent;
  }
  return 1;
}

/**
 *
 */
void
glw_focus_set(glw_t *w)
{
  w->glw_root->gr_current_focus = w;

  while(w->glw_parent != NULL) {
    if(w->glw_parent->glw_focused != w) {
      w->glw_parent->glw_focused = w;
      glw_signal0(w->glw_parent, GLW_SIGNAL_FOCUS_CHILD_CHANGED, w);
      glw_signal0(w, GLW_SIGNAL_FOCUS_SELF, NULL);
    }
    w = w->glw_parent;
  }
}



/**
 *
 */
static void
glw_focus_set_current_by_path(glw_t *w)
{
  while(w->glw_focused != NULL) 
    w = w->glw_focused;
  w->glw_root->gr_current_focus = w;
}


/**
 *
 */
static void
glw_focus_init_widget(glw_t *w0)
{
  glw_t *w = w0;

  while(w->glw_parent != NULL) {
    if(w->glw_parent->glw_focused == NULL) {
      w->glw_parent->glw_focused = w;

      glw_signal0(w->glw_parent, GLW_SIGNAL_FOCUS_CHILD_CHANGED, w);
      glw_signal0(w, GLW_SIGNAL_FOCUS_SELF, NULL);

    } else if(w->glw_parent->glw_focused != w)
      return;
    w = w->glw_parent;
  }

  w0->glw_root->gr_current_focus = w0;
}



/**
 *
 */
static glw_t *
glw_focus_leave0(glw_t *w, glw_t *cur)
{
  glw_t *c, *r;

  c = cur ? TAILQ_NEXT(cur, glw_parent_link) : TAILQ_FIRST(&w->glw_childs);

  for(;;c = TAILQ_NEXT(c, glw_parent_link)) {
    if(c == NULL) {
      if(cur == NULL)
	return NULL;
      c = TAILQ_FIRST(&w->glw_childs);
    }

    if(c == cur)
      return NULL;
    if(c->glw_flags & GLW_FOCUS_BLOCKED)
      continue;
    if(c->glw_flags & GLW_FOCUSABLE)
      return c;
    if(TAILQ_FIRST(&c->glw_childs))
      if((r = glw_focus_leave0(c, NULL)) != NULL)
	return r;
  }
}

/**
 *
 */
static void
glw_focus_leave(glw_t *w)
{
  glw_t *r = NULL;

  if(!glw_path_in_focus(w)) {
    w->glw_parent->glw_focused = glw_focus_leave0(w->glw_parent, w);
    return;
  }

  while(w->glw_parent != NULL) {
    if((r = glw_focus_leave0(w->glw_parent, w)) != NULL)
      break;
    w = w->glw_parent;
  }

  if(r != NULL)
    glw_focus_set(r);
}


/**
 *
 */
static glw_t *
glw_focus_crawl0(glw_t *w, glw_t *cur, int forward)
{
  glw_t *c, *r;

  if(forward) {
    c = cur ? TAILQ_NEXT(cur, glw_parent_link) : TAILQ_FIRST(&w->glw_childs);
  } else {
    c = cur ? TAILQ_PREV(cur, glw_queue, glw_parent_link) : 
      TAILQ_LAST(&w->glw_childs, glw_queue);
  }

  for(; c != NULL; c = forward ? TAILQ_NEXT(c, glw_parent_link) : 
	TAILQ_PREV(c, glw_queue, glw_parent_link)) {

    if(c->glw_flags & GLW_FOCUS_BLOCKED)
      continue;
    if(c->glw_flags & GLW_FOCUSABLE)
      return c;
    if(TAILQ_FIRST(&c->glw_childs))
      if((r = glw_focus_crawl0(c, NULL, forward)) != NULL)
	return r;
  }
  return NULL;
}


/**
 *
 */
static glw_t *
glw_focus_crawl1(glw_t *w, int forward)
{
  glw_t *c, *r;

  c = forward ? TAILQ_FIRST(&w->glw_childs) : 
    TAILQ_LAST(&w->glw_childs, glw_queue);

  for(; c != NULL; c = forward ? TAILQ_NEXT(c, glw_parent_link) : 
	TAILQ_PREV(c, glw_queue, glw_parent_link)) {

    if(!(c->glw_flags & GLW_FOCUS_BLOCKED)) {
      if(c->glw_flags & GLW_FOCUSABLE)
	return c;
      if(TAILQ_FIRST(&c->glw_childs))
	if((r = glw_focus_crawl1(c, forward)) != NULL)
	return r;
    }
  }
  return NULL;
}


/**
 * Used to focus next (or previous) focusable widget.
 */
void
glw_focus_crawl(glw_t *w, int forward)
{
  glw_t *r = NULL;

  while(w->glw_parent != NULL) {
    if((r = glw_focus_crawl0(w->glw_parent, w, forward)) != NULL)
      break;
    w = w->glw_parent;
  }

  if(r == NULL)
    r = glw_focus_crawl1(w, forward);

  if(r != NULL)
    glw_focus_set(r);
}


/**
 *
 */
void
glw_focus_unblock_path(glw_t *w)
{
  glw_t *p = w->glw_parent;

  if(p == NULL || p->glw_focused == w)
    return;

  if(p->glw_focused != NULL)
    p->glw_focused->glw_flags |= GLW_FOCUS_BLOCKED;

  w->glw_flags &= ~GLW_FOCUS_BLOCKED;
  p->glw_focused = w;

  glw_signal0(p, GLW_SIGNAL_FOCUS_CHILD_CHANGED, w);
  glw_signal0(w, GLW_SIGNAL_FOCUS_SELF, NULL);

  if(glw_path_in_focus(w))
    glw_focus_set_current_by_path(w);
}


/**
 *
 */
void
glw_focus_set_if_parent_is_in_focus(glw_t *w)
{
  glw_t *p = w->glw_parent;

  if(p->glw_focused == w)
    return;

  p->glw_focused = w;
  return;

  glw_signal0(p, GLW_SIGNAL_FOCUS_CHILD_CHANGED, w);

  if(glw_path_in_focus(w))
    glw_focus_set_current_by_path(w);
}


/**
 *
 */
int
glw_focus_step(glw_t *w, int forward)
{
  event_t *e;

  if(!glw_path_in_focus(w))
    return 0;

  e = event_create_simple(forward ? EVENT_DOWN : EVENT_UP);


  while(w->glw_focused != NULL) {
    w = w->glw_focused;
    if(w->glw_flags & GLW_FOCUSABLE) {
      if(glw_event_to_widget(w, e, 1))
	break;
    }
  }

  event_unref(e);
  return 1;
}




/**
 *
 */
int
glw_event_to_widget(glw_t *w, event_t *e, int local)
{
  if(glw_event_map_intercept(w, e))
    return 1;

  if(glw_signal0(w, GLW_SIGNAL_EVENT, e))
    return 1;

  return glw_navigate(w, e, local);
}

/**
 *
 */
int
glw_event(glw_root_t *gr, event_t *e)
{
  glw_t *w = gr->gr_current_focus;
  return w != NULL ? glw_event_to_widget(w, e, 0) : 0;
}


/**
 * XXX: Replace with glProject
 */
static void
glw_widget_project(float *m, float *x1, float *x2, float *y1, float *y2)
{
  *x1 = m[12] - m[0];
  *x2 = m[12] + m[0];
  *y1 = m[13] - m[5];
  *y2 = m[13] + m[5];
}




/**
 *
 */
static int
pointer_event0(glw_t *w, glw_pointer_event_t *gpe)
{
  glw_t *c;
  event_t *e;
  float x1, x2, y1, y2;
  glw_pointer_event_t gpe0;

  if(w->glw_matrix != NULL) {
    glw_widget_project(w->glw_matrix, &x1, &x2, &y1, &y2);
    
    if(gpe->x > x1 && gpe->x < x2 && gpe->y > y1 && gpe->y < y2) {

      gpe0.type = gpe->type;
      /* Rescale to widget local coordinates */
      gpe0.x = -1 + 2 * (gpe->x - x1) / (x2 - x1);
      gpe0.y = -1 + 2 * (gpe->y - y1) / (y2 - y1);

      if(glw_signal0(w, GLW_SIGNAL_POINTER_EVENT, &gpe0))
	return 1;

      if(w->glw_flags & GLW_FOCUSABLE) {

	switch(gpe->type) {
	case GLW_POINTER_CLICK:
	  //	  glw_focus_set(w);
	  return 1;

	case GLW_POINTER_RELEASE:
	  e = event_create_simple(EVENT_ENTER);
	  glw_event_to_widget(w, e, 0);
	  event_unref(e);
	  return 1;
	default:
	  break;
	}
      }
    }
  }

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    if(!(c->glw_flags & GLW_FOCUS_BLOCKED) && pointer_event0(c, gpe))
      return 1;
  return 0;
}


/**
 *
 */
void
glw_pointer_event(glw_root_t *gr, glw_t *top, glw_pointer_event_t *gpe)
{
  glw_t *c, *w;
  glw_pointer_event_t gpe0;
  float x1, x2, y1, y2;

  if((w = gr->gr_pointer_grab) != NULL && gpe->type == GLW_POINTER_MOTION) {
    
    if(w->glw_matrix == NULL)
      return;

    glw_widget_project(w->glw_matrix, &x1, &x2, &y1, &y2);
    
    gpe0.type = GLW_POINTER_FOCUS_MOTION;
    gpe0.x = -1 + 2 * GLW_RESCALE(gpe->x, x1, x2);
    gpe0.y = -1 + 2 * GLW_RESCALE(gpe->y, y1, y2);
    
    glw_signal0(w, GLW_SIGNAL_POINTER_EVENT, &gpe0);
    return;
  }

  if(gpe->type == GLW_POINTER_RELEASE)
    gr->gr_pointer_grab = NULL;

  TAILQ_FOREACH(c, &top->glw_childs, glw_parent_link)
    if(!(c->glw_flags & GLW_FOCUS_BLOCKED) && pointer_event0(c, gpe))
      break;
}


/**
 * Render a widget with prior translation and scaling
 */
void
glw_render_TS(glw_t *c, glw_rctx_t *rc, glw_rctx_t *prevrc)
{
  rc->rc_size_x = prevrc->rc_size_x * c->glw_parent_scale.x;
  rc->rc_size_y = prevrc->rc_size_y * c->glw_parent_scale.y;

  glw_PushMatrix(rc, prevrc);

  glw_Translatef(rc, 
		 c->glw_parent_pos.x,
		 c->glw_parent_pos.y,
		 c->glw_parent_pos.z);

  glw_Scalef(rc, 
	     c->glw_parent_scale.x,
	     c->glw_parent_scale.y,
	     c->glw_parent_scale.z);

  glw_signal0(c, GLW_SIGNAL_RENDER, rc);
  glw_PopMatrix();
}


/**
 * Render a widget with prior translation
 */
void
glw_render_T(glw_t *c, glw_rctx_t *rc, glw_rctx_t *prevrc)
{
  glw_PushMatrix(rc, prevrc);

  glw_Translatef(rc, 
		 c->glw_parent_pos.x,
		 c->glw_parent_pos.y,
		 c->glw_parent_pos.z);

  glw_signal0(c, GLW_SIGNAL_RENDER, rc);
  glw_PopMatrix();
}



/**
 *
 */
void
glw_rescale(glw_rctx_t *rc, float t_aspect)
{
  float s_aspect = rc->rc_size_x / rc->rc_size_y;
  float a = s_aspect / t_aspect;

  if(a > 1.0f) {
    a = 1.0 / a;
    glw_Scalef(rc, a, 1.0f, 1.0f);
    rc->rc_size_x *= a;
  } else {
    glw_Scalef(rc, 1.0f, a, 1.0f);
    rc->rc_size_y *= a;
  }
}



/**
 *
 */
int
glw_dispatch_event(uii_t *uii, event_t *e)
{
  glw_root_t *gr = (glw_root_t *)uii;
  int r;

  glw_lock(gr);
  r = glw_event(gr, e);
  glw_unlock(gr);

  return r;
}

const glw_vertex_t align_vertices[] = 
  {
    [GLW_ALIGN_CENTER] = {  0.0,  0.0, 0.0 },
    [GLW_ALIGN_LEFT]   = { -1.0,  0.0, 0.0 },
    [GLW_ALIGN_RIGHT]  = {  1.0,  0.0, 0.0 },
    [GLW_ALIGN_BOTTOM] = {  0.0, -1.0, 0.0 },
    [GLW_ALIGN_TOP]    = {  0.0,  1.0, 0.0 },
  };
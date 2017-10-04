/*
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of  
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <math.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkkeysyms-compat.h>
#include <assert.h>
#include <stdlib.h>
#include "xournal.h"
#include "xo-interface.h"
#include "xo-support.h"
#include "xo-callbacks.h"
#include "xo-misc.h"
#include "xo-file.h"
#include "xo-paint.h"
#include "xo-shapes.h"
#include "xo-image.h"

// some global constants

guint predef_colors_rgba[COLOR_MAX] =
  { 0x000000ff, 0x3333ccff, 0xff0000ff, 0x008000ff,
    0x808080ff, 0x00c0ffff, 0x00ff00ff, 0xff00ffff,
    0xff8000ff, 0xffff00ff, 0xffffffff };

guint predef_bgcolors_rgba[COLOR_MAX] = // meaningless ones set to white
  { 0xffffffff, 0xa0e8ffff, 0xffc0d4ff, 0x80ffc0ff,
    0xffffffff, 0xa0e8ffff, 0x80ffc0ff, 0xffc0d4ff,
    0xffc080ff, 0xffff80ff, 0xffffffff };

double predef_thickness[NUM_STROKE_TOOLS][THICKNESS_MAX] =
  { { 0.42, 0.85, 1.41,  2.26, 5.67 }, // pen thicknesses = 0.15, 0.3, 0.5, 0.8, 2 mm
    { 2.83, 2.83, 8.50, 19.84, 19.84 }, // eraser thicknesses = 1, 3, 7 mm
    { 2.83, 2.83, 8.50, 19.84, 19.84 }, // highlighter thicknesses = 1, 3, 7 mm
  };


void xo_canvas_scroll_to_y_pixels(gdouble y)
{

  gdouble x;
  gdouble oldy;

  // get x
  xo_canvas_get_scroll_offsets_in_pixels(canvas, &x, &oldy);

  goo_canvas_scroll_to(canvas, x, y);

}

void xo_canvas_item_resize(GooCanvasItem  *item, gdouble newWidth, gdouble newHeight, gboolean scaleToFit)
{
  gdouble currentWidth;
  gdouble currentHeight;
  
  // goocanvas resizes widget even if the size has not really changed
  // so make sure we don't waste our time

  g_object_get(item, 
	       "width", &currentWidth,
	       "height", &currentHeight,
	       NULL);

  if (fabs(currentWidth - newWidth) >= 2.0 ||
      fabs(currentHeight - newHeight) >= 2.0) {
    g_object_set(item,
		 "width", newWidth, 
		 "height", newHeight, 
		 NULL);
    if (scaleToFit) {
      g_object_set(item, "scale-to-fit", TRUE, 
		   NULL);
    }
  } 
}




// some manipulation functions

void xo_page_canvas_group_new(Page *pg)
{
  GooCanvasItem *root;
  assert(pg != NULL);
  assert(canvas != NULL);

  root = goo_canvas_get_root_item(canvas);
  assert(root != NULL);

  pg->group =  goo_canvas_group_new (root, NULL);
  assert(pg->group != NULL);
  make_page_clipbox(pg);

}

void xo_page_set_canvas_pixbuf(Page *pg, GdkPixbuf *pix)
{
  gdouble w, h;

  w = gdk_pixbuf_get_width (pix);
  h = gdk_pixbuf_get_height (pix);

  w /= ui.zoom;
  h /= ui.zoom;
  
  /*
  w /= pg->bg->pixbuf_scale;
  h /= pg->bg->pixbuf_scale;
  */

  //  goo_canvas_convert_from_pixels(canvas, &w2, &h2);

  if ( pg->bg->canvas_group != NULL) {
    TRACE_1("--------------We have data\n\n in canvas_group\n");
  }

  pg->bg->canvas_group = goo_canvas_image_new(pg->group,
					      pix,
					      0, 0,   // x,y coordinate in canvas
					      "width", w,
					      "height", h,
					      "scale-to-fit", TRUE,
					      NULL);
  
  if (pg->bg->canvas_pixbuf != NULL) {
    g_object_unref(pg->bg->canvas_pixbuf);
    pg->bg->canvas_pixbuf = NULL;
  }
  pg->bg->canvas_pixbuf = pix;
  g_object_ref(pix);
}

void xo_background_update_pixbuf(Background *bg)
{
  // we need to keep two pixbufs, the one already rendered, and the one that is being rescaled
  // if the one being scaled changes, update the one being displayed
  if (bg->canvas_pixbuf != bg->pixbuf) {
    g_object_set(G_OBJECT(bg->canvas_group), "pixbuf" , bg->pixbuf, NULL);

    if (bg->canvas_pixbuf != NULL) {
      g_object_unref(bg->canvas_pixbuf);
      bg->canvas_pixbuf = NULL;
    }
    
    bg->canvas_pixbuf = bg->pixbuf;
    g_object_ref(bg->pixbuf);
  }
}


struct Page *new_page(struct Page *template)
{

  struct Page *pg = (struct Page *) g_memdup(template, sizeof(struct Page));
  struct Layer *l = g_new(struct Layer, 1);
  GooCanvasItem *root;
  
  l->items = NULL;
  l->nitems = 0;
  pg->layers = g_list_append(NULL, l);
  pg->nlayers = 1;
  pg->bg = (struct Background *)g_memdup(template->bg, sizeof(struct Background));
  pg->bg->canvas_group = NULL;
  if (pg->bg->type == BG_PIXMAP || pg->bg->type == BG_PDF) {
    g_object_ref(pg->bg->pixbuf);
    refstring_ref(pg->bg->filename);
  }

  xo_page_canvas_group_new(pg);
  update_canvas_bg(pg);

  l->group = goo_canvas_group_new ((GooCanvasItem*)pg->group, NULL);

  return pg;
}


void xo_goo_canvas_item_show(GooCanvasItem *item)
{
  if (!goo_canvas_item_is_visible (item)) {
    g_object_set (item,
		  "visibility", GOO_CANVAS_ITEM_VISIBLE,
		  NULL);
  }
}

void xo_goo_canvas_item_hide(GooCanvasItem *item)
{
  if (goo_canvas_item_is_visible (item)) {
    g_object_set (item,
		  "visibility", GOO_CANVAS_ITEM_INVISIBLE,
		  NULL);
  }
}

void xo_canvas_round_position_to_pixel(gdouble *x, gdouble *y)
{
  
  // we need to place pages exactly in a pixel boundary
  // otherwise the PDF backgrounds are antialised --and look blurry--
  // so we convert canvas dimensions to pixels
  // round the pixel, and convert it back to canvas

  goo_canvas_convert_to_pixels(canvas, x, y);
  *y = round(*y);
  *x = round(*x);
  goo_canvas_convert_from_pixels(canvas, x, y);
}

void xo_goo_canvas_item_move_to(GooCanvasItem *item, gdouble x, gdouble y)
{
  gdouble xold;
  gdouble yold;
  g_object_get(item, "x", &xold, "y", &yold, NULL);
  
  if (fabs(x - xold) > 1e-10 || 
      fabs(y - yold) > 1e-10)
    g_object_set(G_OBJECT(item), "x",x,"y",y, NULL);
}

void xo_goo_canvas_item_move_to_pixel_boundary(GooCanvasItem *item)
{
  gdouble x;
  gdouble y;
  gdouble xold;
  gdouble yold;
  g_object_get(item, "x", &x, "y", &y, NULL);
  xold = x;
  yold = y;
  xo_canvas_round_position_to_pixel(&x, &y);

  if (x != xold || y != yold) 
    g_object_set(G_OBJECT(item), "x",x,"y",y, NULL);
}



GdkPixbuf  *xo_goo_canvas_item_pixbuf_get(GooCanvasItem *item)
{
  GdkPixbuf *pix;
  g_object_get(G_OBJECT(item), "pixbuf" , &pix, NULL);
  return (pix);
}


/* Create a page from a background. 
   Note: bg should be an UNREFERENCED background.
   If needed, first duplicate it and increase the refcount of the pixbuf.
*/
struct Page *new_page_with_bg(struct Background *bg, double width, double height)
{

  struct Page *pg = g_new(struct Page, 1);
  struct Layer *l = g_new(struct Layer, 1);
  
  TRACE_1("Starting new page-----------------------------\n");
  l->items = NULL;
  l->nitems = 0;
  pg->layers = g_list_append(NULL, l);
  pg->nlayers = 1;
  pg->bg = bg;
  printf("<<<Bg type [%d]\n", pg->bg->type);
  printf("<<<Bg type [%p]\n", (void*)pg->bg->pixbuf);

  pg->bg->canvas_group = NULL;
  pg->height = height;
  pg->width = width;


  xo_page_canvas_group_new(pg);
  update_canvas_bg(pg);

  l->group = goo_canvas_group_new ((GooCanvasItem*)pg->group, NULL);
  //l->group = (GooCanvasGroup *) gnome_canvas_item_new(
  //    pg->group, gnome_canvas_group_get_type(), NULL);
  
  return pg;

}

// change the current page if necessary for pointer at pt
void set_current_page(gdouble *pt)
{
  gboolean page_change;
  struct Page *tmppage;

  page_change = FALSE;
  tmppage = ui.cur_page;
  while (ui.view_continuous && (pt[1] < - VIEW_CONTINUOUS_SKIP)) {
    if (ui.pageno == 0) break;
    page_change = TRUE;
    ui.pageno--;
    tmppage = g_list_nth_data(journal.pages, ui.pageno);
    pt[1] += tmppage->height + VIEW_CONTINUOUS_SKIP;
  }
  while (ui.view_continuous && (pt[1] > tmppage->height + VIEW_CONTINUOUS_SKIP)) {
    if (ui.pageno == journal.npages-1) break;
    pt[1] -= tmppage->height + VIEW_CONTINUOUS_SKIP;
    page_change = TRUE;
    ui.pageno++;
    tmppage = g_list_nth_data(journal.pages, ui.pageno);
  }
  if (page_change) do_switch_page(ui.pageno, FALSE, FALSE);
}

void realloc_cur_path(int n)
{
  if (n <= ui.cur_path_storage_alloc) return;
  ui.cur_path_storage_alloc = n+100;
  ui.cur_path.coords = g_realloc(ui.cur_path.coords, 2*(n+100)*sizeof(double));
}

void realloc_cur_widths(int n)
{
  if (n <= ui.cur_widths_storage_alloc) return;
  ui.cur_widths_storage_alloc = n+100;
  ui.cur_widths = g_realloc(ui.cur_widths, (n+100)*sizeof(double));
}

// undo utility functions

void prepare_new_undo(void)
{
  struct UndoItem *u;
  // add a new UndoItem on the stack  
  u = (struct UndoItem *)g_malloc(sizeof(struct UndoItem));
  u->next = undo;
  u->multiop = 0;
  undo = u;
  ui.saved = FALSE;
  clear_redo_stack();
}

void clear_redo_stack(void)
{


  struct UndoItem *u;  
  GList *list, *repl;
  struct UndoErasureData *erasure;
  struct Item *it;

  /* Warning: the redo items might reference items from past redo entries,
     which have been destroyed before them. Be careful! As a rule, it's
     safe to destroy data which has been created at the current history step,
     it's unsafe to refer to any data from previous history steps */
  
  while (redo!=NULL) {
    if (redo->type == ITEM_STROKE) {
      goo_canvas_points_unref(redo->item->path);
      if (redo->item->brush.variable_width) g_free(redo->item->widths);
      g_free(redo->item);
      /* the strokes are unmapped, so there are no associated canvas items */
    }
    else if (redo->type == ITEM_TEXT) {
      g_free(redo->item->text);
      g_free(redo->item->font_name);
      g_free(redo->item);
    }
    else if (redo->type == ITEM_IMAGE) {
      g_object_unref(redo->item->image);
      g_free(redo->item->image_png);
      g_free(redo->item);
    }
    else if (redo->type == ITEM_ERASURE || redo->type == ITEM_RECOGNIZER) {
      for (list = redo->erasurelist; list!=NULL; list=list->next) {
        erasure = (struct UndoErasureData *)list->data;
        for (repl = erasure->replacement_items; repl!=NULL; repl=repl->next) {
          it = (struct Item *)repl->data;
          goo_canvas_points_unref(it->path);
          if (it->brush.variable_width) g_free(it->widths);
          g_free(it);
        }
        g_list_free(erasure->replacement_items);
        g_free(erasure);
      }
      g_list_free(redo->erasurelist);
    }
    else if (redo->type == ITEM_NEW_BG_ONE || redo->type == ITEM_NEW_BG_RESIZE
          || redo->type == ITEM_NEW_DEFAULT_BG) {
      if (redo->bg->type == BG_PIXMAP || redo->bg->type == BG_PDF) {
        if (redo->bg->pixbuf!=NULL) 
	  g_object_unref(redo->bg->pixbuf);
        refstring_unref(redo->bg->filename);
      }
      g_free(redo->bg);
    }
    else if (redo->type == ITEM_NEW_PAGE) {
      redo->page->group = NULL;
      delete_page(redo->page);
    }
    else if (redo->type == ITEM_MOVESEL || redo->type == ITEM_REPAINTSEL) {
      g_list_free(redo->itemlist); g_list_free(redo->auxlist);
    }
    else if (redo->type == ITEM_RESIZESEL) {
      g_list_free(redo->itemlist);
    }
    else if (redo->type == ITEM_PASTE) {
      for (list = redo->itemlist; list!=NULL; list=list->next) {
        it = (struct Item *)list->data;
        if (it->type == ITEM_STROKE) {
          goo_canvas_points_unref(it->path);
          if (it->brush.variable_width) g_free(it->widths);
        }
        g_free(it);
      }
      g_list_free(redo->itemlist);
    }
    else if (redo->type == ITEM_NEW_LAYER) {
      g_free(redo->layer);
    }
    else if (redo->type == ITEM_TEXT_EDIT || redo->type == ITEM_TEXT_ATTRIB) {
      g_free(redo->str);
      if (redo->type == ITEM_TEXT_ATTRIB) g_free(redo->brush);
    }

    u = redo;
    redo = redo->next;
    g_free(u);
  }
  update_undo_redo_enabled();


}

void clear_undo_stack(void)
{

  struct UndoItem *u;
  GList *list;
  struct UndoErasureData *erasure;
  
  while (undo!=NULL) {
    // for strokes, items are already in the journal, so we don't free them
    // for erasures, we need to free the dead items
    if (undo->type == ITEM_ERASURE || undo->type == ITEM_RECOGNIZER) {
      for (list = undo->erasurelist; list!=NULL; list=list->next) {
        erasure = (struct UndoErasureData *)list->data;
        if (erasure->item->type == ITEM_STROKE) {
          goo_canvas_points_unref(erasure->item->path);
          if (erasure->item->brush.variable_width) g_free(erasure->item->widths);
        }
        if (erasure->item->type == ITEM_TEXT)
          { g_free(erasure->item->text); g_free(erasure->item->font_name); }
        if (erasure->item->type == ITEM_IMAGE) {
          g_object_unref(erasure->item->image);
          g_free(erasure->item->image_png);
        }
        g_free(erasure->item);
        g_list_free(erasure->replacement_items);
        g_free(erasure);
      }
      g_list_free(undo->erasurelist);
    }
    else if (undo->type == ITEM_NEW_BG_ONE || undo->type == ITEM_NEW_BG_RESIZE
          || undo->type == ITEM_NEW_DEFAULT_BG) {
      if (undo->bg->type == BG_PIXMAP || undo->bg->type == BG_PDF) {
        if (undo->bg->pixbuf!=NULL) g_object_unref(undo->bg->pixbuf);
        refstring_unref(undo->bg->filename);
      }
      g_free(undo->bg);
    }
    else if (undo->type == ITEM_MOVESEL || undo->type == ITEM_REPAINTSEL) {
      g_list_free(undo->itemlist); g_list_free(undo->auxlist);
    }
    else if (undo->type == ITEM_RESIZESEL) {
      g_list_free(undo->itemlist);
    }
    else if (undo->type == ITEM_PASTE) {
      g_list_free(undo->itemlist);
    }
    else if (undo->type == ITEM_DELETE_LAYER) {
      undo->layer->group = NULL;
      delete_layer(undo->layer);
    }
    else if (undo->type == ITEM_DELETE_PAGE) {
      undo->page->group = NULL;
      delete_page(undo->page);
    }
    else if (undo->type == ITEM_TEXT_EDIT || undo->type == ITEM_TEXT_ATTRIB) {
      g_free(undo->str);
      if (undo->type == ITEM_TEXT_ATTRIB) g_free(undo->brush);
    }

    u = undo;
    undo = undo->next;
    g_free(u);
  }
  update_undo_redo_enabled();


}

// free data structures 

void delete_journal(struct Journal *j)
{
  while (j->pages!=NULL) {
    delete_page((struct Page *)j->pages->data);
    j->pages = g_list_delete_link(j->pages, j->pages);
  }
}

void delete_page(struct Page *pg)
{
  struct Layer *l;

  while (pg->layers!=NULL) {
    l = (struct Layer *)pg->layers->data;
    l->group = NULL;
    delete_layer(l);
    pg->layers = g_list_delete_link(pg->layers, pg->layers);
  }
  if (pg->group!=NULL) {
    // removing it deletes it
    goo_canvas_item_remove(pg->group);
  }
  TRACE_1("in the middle\n");
              // this also destroys the background's canvas items
  if (pg->bg->type == BG_PIXMAP || pg->bg->type == BG_PDF) {
    if (pg->bg->pixbuf != NULL) 
      g_object_unref(pg->bg->pixbuf);
    if (pg->bg->canvas_pixbuf != NULL) 
      g_object_unref(pg->bg->canvas_pixbuf);
    if (pg->bg->filename != NULL) 
      refstring_unref(pg->bg->filename);
    pg->bg->pixbuf = NULL;
    pg->bg->canvas_pixbuf = NULL;
    pg->bg->filename = NULL;
  }
  g_free(pg->bg);
  g_free(pg);
}

void delete_layer(struct Layer *l)
{

  struct Item *item;
  
  while (l->items!=NULL) {

    item = (struct Item *)l->items->data;

    if (item->type == ITEM_STROKE && item->path != NULL) 
      goo_canvas_points_unref(item->path);

    if (item->type == ITEM_TEXT) {
      g_free(item->font_name); g_free(item->text);
    }
    if (item->type == ITEM_IMAGE) {
      g_object_unref(item->image);
      g_free(item->image_png);
    }
    // don't need to delete the canvas_item, as it's part of the group destroyed below
    g_free(item);
    l->items = g_list_delete_link(l->items, l->items);
  }
  if (l->group!= NULL)
    g_object_unref(G_OBJECT(l->group));
  g_free(l);
}

// referenced strings

struct Refstring *new_refstring(const char *s)
{
  struct Refstring *rs = g_new(struct Refstring, 1);
  rs->nref = 1;
  if (s!=NULL) rs->s = g_strdup(s);
  else rs->s = NULL;
  rs->aux = NULL;
  return rs;
}

struct Refstring *refstring_ref(struct Refstring *rs)
{
  rs->nref++;
  return rs;
}

void refstring_unref(struct Refstring *rs)
{
  rs->nref--;
  if (rs->nref == 0) {
    if (rs->s!=NULL) g_free(rs->s);
    if (rs->aux!=NULL) g_free(rs->aux);
    g_free(rs);
  }
}


// some helper functions

int finite_sized(double x) // detect unrealistic coordinate values
{
  return (finite(x) && x<1E6 && x>-1E6);
}


void xo_event_get_pointer_coords(GdkEvent *event, gdouble *ret)
{
  double x, y;
  gdk_event_get_coords(event, &x, &y);
  
  //  gnome_canvas_window_to_world(canvas, x, y, ret, ret+1);
  goo_canvas_convert_from_pixels(canvas, &x, &y);

  ret[0] = x - ui.cur_page->hoffset;
  ret[1] = y - ui.cur_page->voffset;

}

void xo_pointer_get_current_coords(gdouble *ret)
{
  GdkEvent *event;
  gdouble x, y;
  GdkModifierType modifier_mask;
  gint ix, iy;

  // gets it in pixels units

  GdkWindow *w;
  w = gtk_widget_get_window(GTK_WIDGET(canvas));
  GdkDeviceManager* manager =  gdk_display_get_device_manager(gdk_window_get_display(w));

  gdk_window_get_device_position(gtk_widget_get_window (GTK_WIDGET(canvas)),  gdk_device_manager_get_client_pointer(manager),
				 &ix, &iy, &modifier_mask);

  x =ix;
  y =iy;

  goo_canvas_convert_from_pixels(canvas, &x, &y);

  if (x < 0) 
    x = 0;
  if (y < 0 )
    y = 0;

  ret[0] = x - ui.cur_page->hoffset;
  ret[1] = y - ui.cur_page->voffset;
}

void fix_xinput_coords(GdkEvent *event)
{
  double *axes, *px, *py, axis_width;
  GdkDevice *device;
  int wx, wy, sx, sy, ix, iy;
  TRACE_1("To be implemented\n");
//  assert(0);
#ifdef ABC
  printf("def abc\n");
  if (event->type == GDK_BUTTON_PRESS || event->type == GDK_BUTTON_RELEASE) {
    axes = event->button.axes;
    px = &(event->button.x);
    py = &(event->button.y);
    device = event->button.device;
  }
  else if (event->type == GDK_MOTION_NOTIFY) {
    axes = event->motion.axes;
    px = &(event->motion.x);
    py = &(event->motion.y);
    device = event->motion.device;
  }
  else return; // nothing we know how to do

  gnome_canvas_get_scroll_offsets(canvas, &sx, &sy);

#ifdef ENABLE_XINPUT_BUGFIX
  // fix broken events with the core pointer's location
  if (!finite_sized(axes[0]) || !finite_sized(axes[1]) || axes[0]==0. || axes[1]==0.) {
    gdk_window_get_pointer(GTK_WIDGET(canvas)->window, &ix, &iy, NULL);
    *px = ix + sx; 
    *py = iy + sy;
  }
  else {
    gdk_window_get_origin(GTK_WIDGET(canvas)->window, &wx, &wy);  
    axis_width = device->axes[0].max - device->axes[0].min;
    if (axis_width>EPSILON)
      *px = (axes[0]/axis_width)*ui.screen_width + sx - wx;
    axis_width = device->axes[1].max - device->axes[1].min;
    if (axis_width>EPSILON)
      *py = (axes[1]/axis_width)*ui.screen_height + sy - wy;
  }
#else
  if (!finite_sized(*px) || !finite_sized(*py) || *px==0. || *py==0.) {
    gdk_window_get_pointer(GTK_WIDGET(canvas)->window, &ix, &iy, NULL);
    *px = ix + sx; 
    *py = iy + sy;
  }
  else {
    /* with GTK+ 2.16 or earlier, the event comes from the parent gdkwindow
       and so needs to be adjusted for scrolling */
    if (gtk_major_version == 2 && gtk_minor_version <= 16) {
      *px += sx;
      *py += sy;
    }
    /* with GTK+ 2.17, events come improperly translated, and the event's
       GdkWindow isn't even the same for ButtonDown as for MotionNotify... */
    if (gtk_major_version == 2 && gtk_minor_version == 17) { // GTK+ 2.17 issues !!
      gdk_window_get_position(GTK_WIDGET(canvas)->window, &wx, &wy);
      *px += sx - wx;
      *py += sy - wy;
    }
  }
#endif
#else
  // This should mostly work now, do nothing
  //fprintf(stderr, "This function needs to be ported...failing...");
  //exit(1);
#endif


}

double get_pressure_multiplier(GdkEvent *event)
{
  gdouble *axes;
  gdouble rawpressure;
  GdkDevice *device;

  if (event->type == GDK_MOTION_NOTIFY) {
    axes = event->motion.axes;
    device = event->motion.device;
  }
  else {
    axes = event->button.axes;
    device = event->button.device;
  }

#ifdef ABC
  // dmg: we don't need any of this. It is way simpler now (I think :)
  if (device == gdk_device_get_core_pointer() || 
      gdk_device_get_n_axes (device) <= 2) 
    return 1.0;

  rawpressure = axes[2]/(device->axes[2].max - device->axes[2].min);

  if (!finite_sized(rawpressure)) 
    return 1.0;

  return ((1-rawpressure)*ui.width_minimum_multiplier + rawpressure*ui.width_maximum_multiplier);
#else
  // let us rewrite this code.
  if (!gdk_device_get_axis(device, axes, GDK_AXIS_PRESSURE, &rawpressure)) {
    return 1.0;
  }
  return ((1-rawpressure)*ui.width_minimum_multiplier + rawpressure*ui.width_maximum_multiplier);
#endif
}

void update_item_bbox(struct Item *item)
{
  int i;
  gdouble *p, h, w;
  
  if (item->type == ITEM_STROKE) {
    item->bbox.left = item->bbox.right = item->path->coords[0];
    item->bbox.top = item->bbox.bottom = item->path->coords[1];
    for (i=1, p=item->path->coords+2; i<item->path->num_points; i++, p+=2)
    {
      if (p[0] < item->bbox.left) item->bbox.left = p[0];
      if (p[0] > item->bbox.right) item->bbox.right = p[0];
      if (p[1] < item->bbox.top) item->bbox.top = p[1];
      if (p[1] > item->bbox.bottom) item->bbox.bottom = p[1];
    }
  } else if (item->type == ITEM_TEXT) {
    if (item->canvas_item!=NULL) {
      GooCanvasBounds bounds;

      goo_canvas_item_get_bounds(item->canvas_item, 
				 &bounds);

      // test our assumptions
      assert(bounds.x2 >= bounds.x1);
      assert(bounds.y2 >= bounds.y1);
      item->bbox.right = bounds.x2;
      item->bbox.bottom = bounds.y2 - ui.cur_page->voffset;

    }  else {
      ; // no bounding box, I guess...
    }
  }// dmg: what happens otherwise?
}

void make_page_clipbox(struct Page *pg)
{


#ifdef ABC
  GooCanvasPathDef *pg_clip;
  
  pg_clip = gnome_canvas_path_def_new_sized(4);
  gnome_canvas_path_def_moveto(pg_clip, 0., 0.);
  gnome_canvas_path_def_lineto(pg_clip, 0., pg->height);
  gnome_canvas_path_def_lineto(pg_clip, pg->width, pg->height);
  gnome_canvas_path_def_lineto(pg_clip, pg->width, 0.);
  gnome_canvas_path_def_closepath(pg_clip);
  gnome_canvas_item_set(GNOME_CANVAS_ITEM(pg->group), "path", pg_clip, NULL);
  gnome_canvas_path_def_unref(pg_clip);
#else

  /*
  printf("------->What the heck\n\n");
  goo_canvas_rect_new(pg->group, 0, 0, pg->width, pg->height, 
		      "line-width", 1.0,
		      "stroke-color", "red", 
		      NULL);
  */
  goo_canvas_set_bounds(canvas, 0, 0, pg->width, pg->height);
#endif

}

void make_canvas_item_one(GooCanvasItem *group, struct Item *item)
{
  GooCanvasPoints points;
  int j;

  if (item->type == ITEM_STROKE) {
    if (!item->brush.variable_width) {
      item->canvas_item = xo_create_path_with_color(group, item->path, item->brush.thickness, item->brush.color_rgba);
    } else {
      // For variable length we use a sequence of segments, each with a different width
      // this is a sequence of segments each with a different width
      item->canvas_item = goo_canvas_group_new(group, NULL);
      points.num_points = 2;
      points.ref_count = 1;

      // set the color of hte item separately
      xo_canvas_item_color_set(item->canvas_item, item->brush.color_rgba);
	   
      for (j = 0; j < item->path->num_points-1; j++) {
        points.coords = item->path->coords+2*j;
	// do not set color, it will be done by the container
	xo_create_path(item->canvas_item, &points, item->widths[j]);
	/*
	goo_canvas_polyline_new(item->canvas_item, FALSE, 0,
				"points", &points,
				"line-cap", CAIRO_LINE_CAP_ROUND, 
				"line-join", CAIRO_LINE_JOIN_ROUND,
				//"fill-color-rgba", item->brush.color_rgba,  
				"stroke-color-rgba", item->brush.color_rgba,  
				"line-width", item->widths[j],
				NULL);
	*/
      }
    }
    // set color
    // we do it separately because this way the items  inherit from the group
  }
  if (item->type == ITEM_TEXT) {
    // goocanvas needs the name of the font followed by its size

    char *font = g_strdup_printf("%s %f", item->font_name, item->font_size);

    item->canvas_item = goo_canvas_text_new(group, item->text,
					    item->bbox.left, 
					    item->bbox.top, 
					    -1,
					    GOO_CANVAS_ANCHOR_NORTH_WEST,
					    "font", font, 
					    "fill-color-rgba", item->brush.color_rgba,
					    NULL);
    g_free(font);

    update_item_bbox(item);

  }
  if (item->type == ITEM_IMAGE) {
    assert(item->image != NULL);
    /*
    printf("   image left %f right %f top %f bottm %f\n",
	   item->bbox.left,
	   item->bbox.right,
	   item->bbox.top,
	   item->bbox.bottom);
	   
    */
    item->canvas_item = goo_canvas_image_new(group, item->image,
					     item->bbox.left, 
					     item->bbox.top, 
                                             "width", item->bbox.right - item->bbox.left,
                                             "height", item->bbox.bottom - item->bbox.top,
                                             "scale-to-fit", TRUE,
					     NULL);

  }
}

void make_canvas_items(void)
{

  struct Page *pg;
  struct Layer *l;
  struct Item *item;
  GList *pagelist, *layerlist, *itemlist;
  
  for (pagelist = journal.pages; pagelist!=NULL; pagelist = pagelist->next) {
    pg = (struct Page *)pagelist->data;
    if (pg->group == NULL) {
      xo_page_canvas_group_new(pg);
    }
    if (pg->bg->canvas_group == NULL) 
      update_canvas_bg(pg);
    
    for (layerlist = pg->layers; layerlist!=NULL; layerlist = layerlist->next) {

      l = (struct Layer *)layerlist->data;
      if (l->group == NULL)
	l->group = goo_canvas_group_new ((GooCanvasItem*)pg->group, NULL);

      for (itemlist = l->items; itemlist!=NULL; itemlist = itemlist->next) {
        item = (struct Item *)itemlist->data;
        if (item->canvas_item == NULL)
          make_canvas_item_one(l->group, item);
      }
    }
  }
}

void xo_draw_vertical_rulings(GooCanvasItem *group, gdouble start, gdouble end,  gdouble spacing, gdouble vlen, guint rgbaColor)
{
  gdouble x, y;

  for (x=start; x< end-1; x+=spacing) {
    goo_canvas_polyline_new_line(group, 
				 x, 0, x, vlen, 
				 "stroke-color-rgba", rgbaColor,
				 "line-width", RULING_THICKNESS,
				 NULL);
  }

}

void xo_draw_horizontal_rulings(GooCanvasItem *group, gdouble start, gdouble end, gdouble spacing, gdouble hlen, guint rgbaColor)
{
  gdouble x, y;

  for (y=start; y<end-1; y+=spacing) {
    goo_canvas_polyline_new_line(group, 
				 0, y, hlen, y,
				 "stroke-color-rgba", rgbaColor,
				 "line-width", RULING_THICKNESS,
				 NULL);
  }
  
}


void xo_page_background_ruling(struct Page *pg)
{
  GooCanvasItem *group;

  pg->bg->canvas_group = goo_canvas_group_new (pg->group, NULL);
  group = pg->bg->canvas_group;
  goo_canvas_item_lower(pg->bg->canvas_group, NULL);
  goo_canvas_rect_new(group, 0, 0, pg->width, pg->height, 
		      "fill-color-rgba", pg->bg->color_rgba, 
		      NULL); 
  
  if (pg->bg->ruling == RULING_NONE) {
    TRACE_1("Exit");
  } else if (pg->bg->ruling == RULING_GRAPH) {
    TRACE_1("Ruling graph\n");
    
    xo_draw_horizontal_rulings(group, RULING_GRAPHSPACING, pg->height, RULING_GRAPHSPACING, pg->width, RULING_COLOR);
    xo_draw_vertical_rulings(group, RULING_GRAPHSPACING, pg->width, RULING_GRAPHSPACING, pg->height, RULING_COLOR);
    
    TRACE_1("End in graphed Ruling");
  } else if (pg->bg->ruling == RULING_LINED) {
    // draw horizontal lines
    
    xo_draw_horizontal_rulings(group, RULING_TOPMARGIN, pg->height, RULING_SPACING, pg->width, RULING_COLOR);
    
    // vertical line
    xo_draw_vertical_rulings(group, RULING_LEFTMARGIN, RULING_LEFTMARGIN+2, 2, pg->height, RULING_MARGIN_COLOR);
    
  } else {
    fprintf(stderr, "invalid  type of ruling in document\n");
  }
  
}

void update_canvas_bg(struct Page *pg)
{
  GooCanvasItem *group;
  GdkPixbuf *scaled_pix;
  double x, y;
  int w, h;
  gboolean is_well_scaled;

  
  if (pg->bg->canvas_group != NULL) {
    // dispose it 
    goo_canvas_item_remove(pg->bg->canvas_group);
    pg->bg->canvas_group = NULL;
  }

  if (pg->bg->type == BG_SOLID) {
    xo_page_background_ruling(pg);
    assert(pg->bg->canvas_pixbuf == NULL);

  } else if (pg->bg->type == BG_PIXMAP) {
    pg->bg->pixbuf_scale = 0;
    assert(pg->bg->pixbuf != NULL);

    xo_page_set_canvas_pixbuf(pg, pg->bg->pixbuf);

    lower_canvas_item_to(pg->group, pg->bg->canvas_group, NULL);
  } else if (pg->bg->type == BG_PDF) {

    if (pg->bg->pixbuf == NULL) 
      return;
    
    is_well_scaled = (fabs(pg->bg->pixel_width - pg->width*ui.zoom) < 2.
		      && fabs(pg->bg->pixel_height - pg->height*ui.zoom) < 2.);
    
    // dmg: the following code is a port from the old one
    // the problem is that in both cases it needs to be resized!!! 
    // So I don't get why we do this.   XXXXXXXXX
    if (is_well_scaled) {
      // then don't resize
      xo_page_set_canvas_pixbuf(pg, pg->bg->pixbuf);
    } else {
      // insert resizing
      xo_page_set_canvas_pixbuf(pg, pg->bg->pixbuf);
    }
    lower_canvas_item_to(pg->group, GOO_CANVAS_ITEM(pg->bg->canvas_group), NULL);
  } else {
    fprintf(stderr, "invalid  type of background in document\n");
  }
}

gboolean is_visible(struct Page *pg)
{
  GtkAdjustment *v_adj;
  double ytop, ybot;
  
  if (!ui.view_continuous) 
    return (pg == ui.cur_page);
  v_adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(canvas));
  ytop = gtk_adjustment_get_value(v_adj)/ui.zoom;
  ybot = (gtk_adjustment_get_value(v_adj) + gtk_adjustment_get_page_size(v_adj)) / ui.zoom;

  return (MAX(ytop, pg->voffset) < MIN(ybot, pg->voffset+pg->height));

}

void rescale_bg_pixmaps(void)
{
  GList *pglist;
  struct Page *pg;
  GdkPixbuf *pix;
  gboolean is_well_scaled;
  gdouble width;
  gdouble zoom_to_request;
  int i=0;

  for (pglist = journal.pages; pglist!=NULL; pglist = pglist->next) {
    i++;
    pg = (struct Page *)pglist->data;
    // in progressive mode we scale only visible pages
    if (ui.progressive_bg && !is_visible(pg)) 
      continue;

    // goo_canvas supports subpixel rendering. that is means we are
    // capable of placing items in subpixel locations. This problematic
    // because it antialises the backgrounds.
    // to solve this problem we need to move the pages to precise 
    // pixel boundaries
    xo_goo_canvas_item_move_to_pixel_boundary(ui.cur_page->group);

    if (pg->bg->type == BG_PIXMAP && pg->bg->canvas_group!=NULL) {

      xo_background_update_pixbuf(pg->bg);
      pg->bg->pixbuf_scale = 0;

    } else if (pg->bg->type == BG_PDF) { 
      // make pixmap scale to correct size if current one is wrong
      is_well_scaled = (fabs(pg->bg->pixel_width - pg->width*ui.zoom) < 2.
                     && fabs(pg->bg->pixel_height - pg->height*ui.zoom) < 2.);
      if (pg->bg->canvas_group != NULL && !is_well_scaled) {
	
	g_object_get(pg->bg->canvas_group, "width", &width, NULL);
	
	//	TRACE_2(" ---------getting width [%f]\n", width)

	if (width > 0) {
	  // I think it means the canvas has been instantiated...
	  // so resize
	  xo_canvas_item_resize(pg->bg->canvas_group, pg->width, pg->height, TRUE);
	  // dmg: should we update pixel_width and pixel_height in bg? XXX
	  // ahh, this is the autorescaling in case that we don't have 
	  // progressive backgrounds. pixel_width is only changed when the actual
	  // pixmap is updated.
	}

      }
      // request an asynchronous update to a better pixmap if needed
      zoom_to_request = MIN(ui.zoom, MAX_SAFE_RENDER_DPI/72.0);
      if (pg->bg->pixbuf_scale != zoom_to_request) {
	TRACE;
	if (add_bgpdf_request(pg->bg->file_page_seq, zoom_to_request))
	  pg->bg->pixbuf_scale = zoom_to_request;
      }
    } else {
      // do nothing. simply skip this page
      ;
    }
  }
}

gboolean have_intersect(struct BBox *a, struct BBox *b)
{
  return (MAX(a->top, b->top) <= MIN(a->bottom, b->bottom)) &&
         (MAX(a->left, b->left) <= MIN(a->right, b->right));
}

/* In libgnomecanvas 2.10.0, the lower/raise functions fail to update
   correctly the end of the group's item list. We try to work around this.
   DON'T USE gnome_canvas_item_raise/lower directly !! */

void lower_canvas_item_to(GooCanvasItem *g, GooCanvasItem *item, GooCanvasItem *after)
{

#ifdef ABC
  int i1, i2;
  
  i1 = g_list_index(g->item_list, item);

  if (i1 == -1) 
    return;
  
  if (after == NULL) 
    i2 = -1;
  else 
    i2 = g_list_index(g->item_list, after);

  if (i1 < i2) 
    goo_canvas_item_raise(item, i2-i1);
  if (i1 > i2+1) 
    goo_canvas_item_lower(item, i1-i2-1);
  // BUGFIX for libgnomecanvas
  g->item_list_end = g_list_last(g->item_list);

#else
  // we want item just above after

  goo_canvas_item_lower(item, NULL);
  if (after != NULL) {
    goo_canvas_item_raise(item, after);
  }

#endif


}

void xo_rgb_to_GdkColor(guint rgba, GdkColor *color)
{
  color->pixel = 0;
  color->red = ((rgba>>24)&0xff)*0x101;
  color->green = ((rgba>>16)&0xff)*0x101;
  color->blue = ((rgba>>8)&0xff)*0x101;
}

guint32 xo_GdkColor_to_rgba(GdkColor gdkcolor, guint16 alpha) 
{
  guint32 rgba =  ((gdkcolor.red   & 0xff00) << 16) |
                  ((gdkcolor.green & 0xff00) << 8)  |
                  ((gdkcolor.blue  & 0xff00) )      |
                  ((alpha & 0xff00) >> 8);

  return rgba;
}

void xo_rgba_to_GdkRGBA(guint rgba, GdkRGBA *color)
{
  color->red =   ((rgba>>24) & 0xff) / 255.0;
  color->green = ((rgba>>16) & 0xff) / 255.0;
  color->blue =  ((rgba>>8) & 0xff) / 255.0;
  color->alpha = (rgba & 0xff) / 255.0;
}

guint32 xo_GdkRGBA_to_rgba(GdkRGBA *gdkcolor)
{
  // from floats to integers
  guint red = gdkcolor->red * 255;
  guint green = gdkcolor->green * 255;
  guint blue = gdkcolor->blue * 255;
  guint alpha = gdkcolor->alpha * 255;

  guint32 rgba =  (red    << 24) |
		   (green  << 16 ) |
		   (blue   << 8)   |
		  alpha;

  return rgba;
}

// some interface functions

void update_thickness_buttons(void)
{
  if (ui.selection!=NULL || ui.toolno[ui.cur_mapping] >= NUM_STROKE_TOOLS) {
    gtk_toggle_tool_button_set_active(
      GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonThicknessOther")), TRUE);
  } else 
  switch (ui.cur_brush->thickness_no) {
    case THICKNESS_FINE:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonFine")), TRUE);
      break;
    case THICKNESS_MEDIUM:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonMedium")), TRUE);
      break;
    case THICKNESS_THICK:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonThick")), TRUE);
      break;
    default:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonThicknessOther")), TRUE);
  }
}

void update_color_buttons(void)
{
  GdkRGBA gdkcolor;
  GtkColorChooser *colorbutton;
  
  if (ui.selection!=NULL || (ui.toolno[ui.cur_mapping] != TOOL_PEN 
      && ui.toolno[ui.cur_mapping] != TOOL_HIGHLIGHTER && ui.toolno[ui.cur_mapping] != TOOL_TEXT)) {
    gtk_toggle_tool_button_set_active(
      GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonColorOther")), TRUE);
  } else
  switch (ui.cur_brush->color_no) {
    case COLOR_BLACK:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonBlack")), TRUE);
      break;
    case COLOR_BLUE:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonBlue")), TRUE);
      break;
    case COLOR_RED:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonRed")), TRUE);
      break;
    case COLOR_GREEN:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonGreen")), TRUE);
      break;
    case COLOR_GRAY:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonGray")), TRUE);
      break;
    case COLOR_LIGHTBLUE:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonLightBlue")), TRUE);
      break;
    case COLOR_LIGHTGREEN:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonLightGreen")), TRUE);
      break;
    case COLOR_MAGENTA:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonMagenta")), TRUE);
      break;
    case COLOR_ORANGE:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonOrange")), TRUE);
      break;
    case COLOR_YELLOW:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonYellow")), TRUE);
      break;
    case COLOR_WHITE:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonWhite")), TRUE);
      break;
    default:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonColorOther")), TRUE);
  }

  colorbutton = GTK_COLOR_CHOOSER(GET_COMPONENT("buttonColorChooser"));
  assert(colorbutton != NULL);
  if ((ui.toolno[ui.cur_mapping] != TOOL_PEN && 
       ui.toolno[ui.cur_mapping] != TOOL_HIGHLIGHTER && 
       ui.toolno[ui.cur_mapping] != TOOL_TEXT))
    gdkcolor.red = gdkcolor.blue = gdkcolor.green = 0;
  else  {
    xo_rgba_to_GdkRGBA(ui.cur_brush->color_rgba, &gdkcolor);
  }

  gtk_color_chooser_set_rgba(colorbutton, &gdkcolor);
  if (ui.toolno[ui.cur_mapping] == TOOL_HIGHLIGHTER) {
    /*
      //We don't need this any more. it should be an assertions
      //rather than a set 
    gtk_color_chooser_set_alpha(colorbutton,
      (ui.cur_brush->color_rgba&0xff)*0x101);
    */
    gtk_color_chooser_set_use_alpha(colorbutton, TRUE);
  } else {
    /*
      //We don't need this any more. it should be an assertions
      //rather than a set 
    gtk_color_chooser_set_alpha(colorbutton, 0xffff);
    */
    gtk_color_chooser_set_use_alpha(colorbutton, FALSE);
  }


}

void update_tool_buttons(void)
{
  switch(ui.toolno[ui.cur_mapping]) {
    case TOOL_PEN:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonPen")), TRUE);
      break;
    case TOOL_ERASER:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonEraser")), TRUE);
      break;
    case TOOL_HIGHLIGHTER:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonHighlighter")), TRUE);
      break;
    case TOOL_TEXT:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonText")), TRUE);
      break;
    case TOOL_IMAGE:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonImage")), TRUE);
      break;
    case TOOL_SELECTREGION:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonSelectRegion")), TRUE);
      break;
    case TOOL_SELECTRECT:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonSelectRectangle")), TRUE);
      break;
    case TOOL_VERTSPACE:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonVerticalSpace")), TRUE);
      break;
    case TOOL_HAND:
      gtk_toggle_tool_button_set_active(
        GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonHand")), TRUE);
      break;
  }
    
  gtk_toggle_tool_button_set_active(
      GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonRuler")), 
      ui.toolno[ui.cur_mapping]<NUM_STROKE_TOOLS && ui.cur_brush->ruler);
  gtk_toggle_tool_button_set_active(
      GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonReco")), 
      ui.toolno[ui.cur_mapping]<NUM_STROKE_TOOLS && ui.cur_brush->recognizer);

  update_thickness_buttons();
  update_color_buttons();
}

void update_tool_menu(void)
{
  switch(ui.toolno[0]) {
    case TOOL_PEN:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("toolsPen")), TRUE);
      break;
    case TOOL_ERASER:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("toolsEraser")), TRUE);
      break;
    case TOOL_HIGHLIGHTER:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("toolsHighlighter")), TRUE);
      break;
    case TOOL_TEXT:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("toolsText")), TRUE);
      break;
    case TOOL_IMAGE:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("toolsImage")), TRUE);
      break;
    case TOOL_SELECTREGION:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("toolsSelectRegion")), TRUE);
      break;
    case TOOL_SELECTRECT:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("toolsSelectRectangle")), TRUE);
      break;
    case TOOL_VERTSPACE:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("toolsVerticalSpace")), TRUE);
      break;
    case TOOL_HAND:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("toolsHand")), TRUE);
      break;
  }

  gtk_check_menu_item_set_active(
      GTK_CHECK_MENU_ITEM(GET_COMPONENT("toolsRuler")), 
      ui.toolno[0]<NUM_STROKE_TOOLS && ui.brushes[0][ui.toolno[0]].ruler);
  gtk_check_menu_item_set_active(
      GTK_CHECK_MENU_ITEM(GET_COMPONENT("toolsReco")), 
      ui.toolno[0]<NUM_STROKE_TOOLS && ui.brushes[0][ui.toolno[0]].recognizer);
}

void update_ruler_indicator(void)
{
  gtk_toggle_tool_button_set_active(
      GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonRuler")), 
      ui.toolno[ui.cur_mapping]<NUM_STROKE_TOOLS && ui.cur_brush->ruler);
  gtk_toggle_tool_button_set_active(
      GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonReco")), 
      ui.toolno[ui.cur_mapping]<NUM_STROKE_TOOLS && ui.cur_brush->recognizer);
  gtk_check_menu_item_set_active(
      GTK_CHECK_MENU_ITEM(GET_COMPONENT("toolsRuler")), 
      ui.toolno[0]<NUM_STROKE_TOOLS && ui.brushes[0][ui.toolno[0]].ruler);
  gtk_check_menu_item_set_active(
      GTK_CHECK_MENU_ITEM(GET_COMPONENT("toolsReco")), 
      ui.toolno[0]<NUM_STROKE_TOOLS && ui.brushes[0][ui.toolno[0]].recognizer);
}

void update_color_menu(void)
{
  if (ui.selection!=NULL || (ui.toolno[ui.cur_mapping] != TOOL_PEN 
    && ui.toolno[ui.cur_mapping] != TOOL_HIGHLIGHTER && ui.toolno[ui.cur_mapping] != TOOL_TEXT)) {
    gtk_check_menu_item_set_active(
      GTK_CHECK_MENU_ITEM(GET_COMPONENT("colorNA")), TRUE);
  } else
  switch (ui.cur_brush->color_no) {
    case COLOR_BLACK:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("colorBlack")), TRUE);
      break;
    case COLOR_BLUE:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("colorBlue")), TRUE);
      break;
    case COLOR_RED:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("colorRed")), TRUE);
      break;
    case COLOR_GREEN:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("colorGreen")), TRUE);
      break;
    case COLOR_GRAY:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("colorGray")), TRUE);
      break;
    case COLOR_LIGHTBLUE:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("colorLightBlue")), TRUE);
      break;
    case COLOR_LIGHTGREEN:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("colorLightGreen")), TRUE);
      break;
    case COLOR_MAGENTA:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("colorMagenta")), TRUE);
      break;
    case COLOR_ORANGE:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("colorOrange")), TRUE);
      break;
    case COLOR_YELLOW:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("colorYellow")), TRUE);
      break;
    case COLOR_WHITE:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("colorWhite")), TRUE);
      break;
    default:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("colorNA")), TRUE);
  }
}

void update_pen_props_menu(void)
{
  switch(ui.brushes[0][TOOL_PEN].thickness_no) {
    case THICKNESS_VERYFINE:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("penthicknessVeryFine")), TRUE);
      break;
    case THICKNESS_FINE:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("penthicknessFine")), TRUE);
      break;
    case THICKNESS_MEDIUM:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("penthicknessMedium")), TRUE);
      break;
    case THICKNESS_THICK:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("penthicknessThick")), TRUE);
      break;
    case THICKNESS_VERYTHICK:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("penthicknessVeryThick")), TRUE);
      break;
  }
}

void update_eraser_props_menu(void)
{
  switch (ui.brushes[0][TOOL_ERASER].thickness_no) {
    case THICKNESS_FINE:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("eraserFine")), TRUE);
      break;
    case THICKNESS_MEDIUM:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("eraserMedium")), TRUE);
      break;
    case THICKNESS_THICK:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("eraserThick")), TRUE);
      break;
  }
  
  gtk_check_menu_item_set_active(
    GTK_CHECK_MENU_ITEM(GET_COMPONENT("eraserStandard")),
    ui.brushes[0][TOOL_ERASER].tool_options == TOOLOPT_ERASER_STANDARD);
  gtk_check_menu_item_set_active(
    GTK_CHECK_MENU_ITEM(GET_COMPONENT("eraserWhiteout")),
    ui.brushes[0][TOOL_ERASER].tool_options == TOOLOPT_ERASER_WHITEOUT);
  gtk_check_menu_item_set_active(
    GTK_CHECK_MENU_ITEM(GET_COMPONENT("eraserDeleteStrokes")),
    ui.brushes[0][TOOL_ERASER].tool_options == TOOLOPT_ERASER_STROKES);
}

void update_highlighter_props_menu(void)
{
  switch (ui.brushes[0][TOOL_HIGHLIGHTER].thickness_no) {
    case THICKNESS_FINE:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("highlighterFine")), TRUE);
      break;
    case THICKNESS_MEDIUM:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("highlighterMedium")), TRUE);
      break;
    case THICKNESS_THICK:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("highlighterThick")), TRUE);
      break;
  }
}

void update_mappings_menu_linkings(void)
{
  switch (ui.linked_brush[1]) {
    case BRUSH_LINKED:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button2LinkBrush")), TRUE);
      break;
    case BRUSH_COPIED:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button2CopyBrush")), TRUE);
      break;
    case BRUSH_STATIC:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button2NABrush")), TRUE);
      break;
  }
  switch (ui.linked_brush[2]) {
    case BRUSH_LINKED:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button3LinkBrush")), TRUE);
      break;
    case BRUSH_COPIED:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button3CopyBrush")), TRUE);
      break;
    case BRUSH_STATIC:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button3NABrush")), TRUE);
      break;
  }
}

void update_mappings_menu(void)
{
  // don't follow the xinput setting, we aren't using it in wayland.
  gtk_widget_set_sensitive(GET_COMPONENT("optionsButtonMappings"), TRUE);
  gtk_widget_set_sensitive(GET_COMPONENT("optionsPressureSensitive"), TRUE);
  gtk_check_menu_item_set_active(
    GTK_CHECK_MENU_ITEM(GET_COMPONENT("optionsButtonMappings")), ui.use_erasertip);
  gtk_check_menu_item_set_active(
    GTK_CHECK_MENU_ITEM(GET_COMPONENT("optionsPressureSensitive")), ui.pressure_sensitivity);

  switch(ui.toolno[1]) {
    case TOOL_PEN:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button2Pen")), TRUE);
      break;
    case TOOL_ERASER:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button2Eraser")), TRUE);
      break;
    case TOOL_HIGHLIGHTER:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button2Highlighter")), TRUE);
      break;
    case TOOL_TEXT:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button2Text")), TRUE);
      break;
    case TOOL_IMAGE:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button2Image")), TRUE);
      break;
    case TOOL_SELECTREGION:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button2SelectRegion")), TRUE);
      break;
    case TOOL_SELECTRECT:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button2SelectRectangle")), TRUE);
      break;
    case TOOL_VERTSPACE:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button2VerticalSpace")), TRUE);
      break;
  }
  switch(ui.toolno[2]) {
    case TOOL_PEN:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button3Pen")), TRUE);
      break;
    case TOOL_ERASER:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button3Eraser")), TRUE);
      break;
    case TOOL_HIGHLIGHTER:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button3Highlighter")), TRUE);
      break;
    case TOOL_TEXT:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button3Text")), TRUE);
      break;
    case TOOL_IMAGE:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button3Image")), TRUE);
      break;
    case TOOL_SELECTREGION:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button3SelectRegion")), TRUE);
      break;
    case TOOL_SELECTRECT:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button3SelectRectangle")), TRUE);
      break;
    case TOOL_VERTSPACE:
      gtk_check_menu_item_set_active(
        GTK_CHECK_MENU_ITEM(GET_COMPONENT("button3VerticalSpace")), TRUE);
      break;
  }
  update_mappings_menu_linkings();
}

void do_switch_page(int pg, gboolean rescroll, gboolean refresh_all)
{


  int i, cx, cy;
  struct Layer *layer;
  GList *list;
  
  ui.pageno = pg;

  /* re-show all the layers of the old page */
  if (ui.cur_page != NULL)
    for (i=0, list = ui.cur_page->layers; list!=NULL; i++, list = list->next) {
      layer = (struct Layer *)list->data;
      if (layer->group!=NULL)
        xo_goo_canvas_item_show(layer->group);
    }
  
  ui.cur_page = g_list_nth_data(journal.pages, ui.pageno);
  ui.layerno = ui.cur_page->nlayers-1;
  ui.cur_layer = (struct Layer *)(g_list_last(ui.cur_page->layers)->data);
  update_page_stuff();

  if (ui.progressive_bg) {
    rescale_bg_pixmaps();
  }
 
  if (rescroll) { // scroll and force a refresh
/* -- this seems to cause some display bugs ??
    gtk_adjustment_set_value(gtk_layout_get_vadjustment(GTK_LAYOUT(canvas)),
      ui.cur_page->voffset*ui.zoom);  */

#ifdef ABC    
    gnome_canvas_get_scroll_offsets(canvas, &cx, &cy);
    cy = ui.cur_page->voffset*ui.zoom;
    gnome_canvas_scroll_to(canvas, cx, cy);
#else
    //    xo_canvas_scroll_to_y_pixels(ui.cur_page->voffset*ui.zoom);
    xo_canvas_scroll_to_y_pixels(ui.cur_page->voffset);
#endif
    
    if (refresh_all)  {
      //gnome_canvas_set_pixels_per_unit(canvas, ui.zoom);
      xo_canvas_set_pixels_per_unit();
    } else if (!ui.view_continuous) {
      //      gnome_canvas_item_move(GNOME_CANVAS_ITEM(ui.cur_page->group), 0., 0.);
      xo_goo_canvas_item_move_to(ui.cur_page->group, 0.0, 0.0);
    }

  }
}



void update_page_stuff(void)
{
  gchar tmp[10];
  GtkComboBoxText *layerbox;
  int i;
  GList *pglist;
  GtkSpinButton *spin;
  struct Page *pg;
  double vertpos, maxwidth;
  
  // move the page groups to their rightful locations or hide them
  if (ui.view_continuous) {

    vertpos = 0.; 
    maxwidth = 0.;
    for (i=0, pglist = journal.pages; pglist!=NULL; i++, pglist = pglist->next) {
      pg = (struct Page *)pglist->data;
      if (pg->group!=NULL) {
        pg->hoffset = 0.; 
	pg->voffset = vertpos;

	xo_goo_canvas_item_move_to(pg->group, pg->hoffset, pg->voffset);
	xo_goo_canvas_item_move_to_pixel_boundary(pg->group);

//        gnome_canvas_item_set(GNOME_CANVAS_ITEM(pg->group), 
//            "x", pg->hoffset, "y", pg->voffset, NULL);
        xo_goo_canvas_item_show(pg->group);

      }

      vertpos += pg->height + VIEW_CONTINUOUS_SKIP;

      if (pg->width > maxwidth) 
	maxwidth = pg->width;
    }
    vertpos -= VIEW_CONTINUOUS_SKIP;
    goo_canvas_set_bounds(canvas, 0, 0, maxwidth, vertpos);

  } else {
    for (pglist = journal.pages; pglist!=NULL; pglist = pglist->next) {
      pg = (struct Page *)pglist->data;
      if (pg == ui.cur_page && pg->group!=NULL) {
        pg->hoffset = 0.; 
	pg->voffset = 0.;
	xo_goo_canvas_item_move_to(pg->group, pg->hoffset, pg->voffset);
        xo_goo_canvas_item_show(pg->group);
      } else {
        if (pg->group!=NULL) 
	  xo_goo_canvas_item_hide(pg->group);
      }
    }
    goo_canvas_set_bounds(canvas, 0, 0, ui.cur_page->width, ui.cur_page->height);
  }

  // update the page / layer info at bottom of screen

  spin = GTK_SPIN_BUTTON(GET_COMPONENT("spinPageNo"));
  ui.in_update_page_stuff = TRUE; // avoid a bad retroaction
  gtk_spin_button_set_range(spin, 1, journal.npages+1);
    /* npages+1 will be used to create a new page at end */
  gtk_spin_button_set_value(spin, ui.pageno+1);
  g_snprintf(tmp, 10, _(" of %d"), journal.npages);
  gtk_label_set_text(GTK_LABEL(GET_COMPONENT("labelNumpages")), tmp);

  layerbox = GTK_COMBO_BOX_TEXT(GET_COMPONENT("comboLayer"));
  if (ui.layerbox_length == 0) {
    gtk_combo_box_text_prepend_text(layerbox, _("Background"));
    ui.layerbox_length++;
  }
  while (ui.layerbox_length > ui.cur_page->nlayers+1) {
    gtk_combo_box_text_remove(layerbox, 0);
    ui.layerbox_length--;
  }
  while (ui.layerbox_length < ui.cur_page->nlayers+1) {
    g_snprintf(tmp, 10, _("Layer %d"), ui.layerbox_length++);
    gtk_combo_box_text_prepend_text(layerbox, tmp);
  }
  gtk_combo_box_set_active(GTK_COMBO_BOX(layerbox), ui.cur_page->nlayers-1-ui.layerno);
  ui.in_update_page_stuff = FALSE;
  
  gtk_container_forall(GTK_CONTAINER(layerbox), xo_unset_focus, NULL);
  
  // update the paper-style menu radio buttons
  
  if (ui.view_continuous)
    gtk_check_menu_item_set_active(
       GTK_CHECK_MENU_ITEM(GET_COMPONENT("viewContinuous")), TRUE);
  else
    gtk_check_menu_item_set_active(
       GTK_CHECK_MENU_ITEM(GET_COMPONENT("viewOnePage")), TRUE);

  if (ui.cur_page->bg->type == BG_SOLID && !ui.bg_apply_all_pages) {
    switch (ui.cur_page->bg->color_no) {
      case COLOR_WHITE:
        gtk_check_menu_item_set_active(
          GTK_CHECK_MENU_ITEM(GET_COMPONENT("papercolorWhite")), TRUE);
        break;
      case COLOR_YELLOW:
        gtk_check_menu_item_set_active(
          GTK_CHECK_MENU_ITEM(GET_COMPONENT("papercolorYellow")), TRUE);
        break;
      case COLOR_RED:
        gtk_check_menu_item_set_active(
          GTK_CHECK_MENU_ITEM(GET_COMPONENT("papercolorPink")), TRUE);
        break;
      case COLOR_ORANGE:
        gtk_check_menu_item_set_active(
          GTK_CHECK_MENU_ITEM(GET_COMPONENT("papercolorOrange")), TRUE);
        break;
      case COLOR_BLUE:
        gtk_check_menu_item_set_active(
          GTK_CHECK_MENU_ITEM(GET_COMPONENT("papercolorBlue")), TRUE);
        break;
      case COLOR_GREEN:
        gtk_check_menu_item_set_active(
          GTK_CHECK_MENU_ITEM(GET_COMPONENT("papercolorGreen")), TRUE);
        break;
      default:
        gtk_check_menu_item_set_active(
          GTK_CHECK_MENU_ITEM(GET_COMPONENT("papercolorNA")), TRUE);
        break;
    }
    switch (ui.cur_page->bg->ruling) {
      case RULING_NONE:
        gtk_check_menu_item_set_active(
          GTK_CHECK_MENU_ITEM(GET_COMPONENT("paperstylePlain")), TRUE);
        break;
      case RULING_LINED:
        gtk_check_menu_item_set_active(
          GTK_CHECK_MENU_ITEM(GET_COMPONENT("paperstyleLined")), TRUE);
        break;
      case RULING_RULED:
        gtk_check_menu_item_set_active(
          GTK_CHECK_MENU_ITEM(GET_COMPONENT("paperstyleRuled")), TRUE);
        break;
      case RULING_GRAPH:
        gtk_check_menu_item_set_active(
          GTK_CHECK_MENU_ITEM(GET_COMPONENT("paperstyleGraph")), TRUE);
        break;
    }
  } else {
    gtk_check_menu_item_set_active(
      GTK_CHECK_MENU_ITEM(GET_COMPONENT("papercolorNA")), TRUE);
    gtk_check_menu_item_set_active(
      GTK_CHECK_MENU_ITEM(GET_COMPONENT("paperstyleNA")), TRUE);
  }
  
  // enable/disable the page/layer menu items and toolbar buttons

  gtk_widget_set_sensitive(GET_COMPONENT("journalPaperColor"), 
     ui.cur_page->bg->type == BG_SOLID || ui.bg_apply_all_pages);
  gtk_widget_set_sensitive(GET_COMPONENT("journalSetAsDefault"),
     ui.cur_page->bg->type == BG_SOLID);
  
  gtk_widget_set_sensitive(GET_COMPONENT("viewFirstPage"), ui.pageno!=0);
  gtk_widget_set_sensitive(GET_COMPONENT("viewPreviousPage"), ui.pageno!=0);
  gtk_widget_set_sensitive(GET_COMPONENT("viewNextPage"), TRUE);
  gtk_widget_set_sensitive(GET_COMPONENT("viewLastPage"), ui.pageno!=journal.npages-1);
  gtk_widget_set_sensitive(GET_COMPONENT("buttonFirstPage"), ui.pageno!=0);
  gtk_widget_set_sensitive(GET_COMPONENT("buttonPreviousPage"), ui.pageno!=0);
  gtk_widget_set_sensitive(GET_COMPONENT("buttonNextPage"), TRUE);
  gtk_widget_set_sensitive(GET_COMPONENT("buttonLastPage"), ui.pageno!=journal.npages-1);
  
  gtk_widget_set_sensitive(GET_COMPONENT("viewShowLayer"), ui.layerno!=ui.cur_page->nlayers-1);
  gtk_widget_set_sensitive(GET_COMPONENT("viewHideLayer"), ui.layerno>=0);

  gtk_widget_set_sensitive(GET_COMPONENT("editPaste"), ui.cur_layer!=NULL);
  gtk_widget_set_sensitive(GET_COMPONENT("buttonPaste"), ui.cur_layer!=NULL);
}

void update_toolbar_and_menu(void)
{
  update_tool_buttons(); // takes care of other toolbar buttons as well  
  update_tool_menu();
  update_color_menu();
  update_pen_props_menu();
  update_eraser_props_menu();
  update_highlighter_props_menu();
  update_mappings_menu();

  gtk_toggle_tool_button_set_active(
    GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonFullscreen")), ui.fullscreen);
  gtk_check_menu_item_set_active(
    GTK_CHECK_MENU_ITEM(GET_COMPONENT("viewFullscreen")), ui.fullscreen);
}

void update_file_name(char *filename)
{
  gchar tmp[100], *p;
  if (ui.filename != NULL) g_free(ui.filename);
  ui.filename = filename;
  if (filename == NULL) {
    gtk_window_set_title(GTK_WINDOW (winMain), _("Xournal"));
    return;
  }
  p = g_utf8_strrchr(filename, -1, '/');
  if (p == NULL) p = filename; 
  else p = g_utf8_next_char(p);
  g_snprintf(tmp, 100, _("Xournal - %s"), p);
  gtk_window_set_title(GTK_WINDOW (winMain), tmp);
  new_mru_entry(filename);

  if (filename[0]=='/') {
    if (ui.default_path!=NULL) g_free(ui.default_path);
    ui.default_path = g_path_get_dirname(filename);
  }
}

void update_undo_redo_enabled(void)
{
  gtk_widget_set_sensitive(GET_COMPONENT("editUndo"), undo!=NULL);
  gtk_widget_set_sensitive(GET_COMPONENT("editRedo"), redo!=NULL);
  gtk_widget_set_sensitive(GET_COMPONENT("buttonUndo"), undo!=NULL);
  gtk_widget_set_sensitive(GET_COMPONENT("buttonRedo"), redo!=NULL);
}

void update_copy_paste_enabled(void)
{
  gtk_widget_set_sensitive(GET_COMPONENT("editCut"), ui.selection!=NULL);
  gtk_widget_set_sensitive(GET_COMPONENT("editCopy"), ui.selection!=NULL);
  gtk_widget_set_sensitive(GET_COMPONENT("editPaste"), ui.cur_item_type!=ITEM_TEXT);
  gtk_widget_set_sensitive(GET_COMPONENT("editDelete"), ui.selection!=NULL);
  gtk_widget_set_sensitive(GET_COMPONENT("buttonCut"), ui.selection!=NULL);
  gtk_widget_set_sensitive(GET_COMPONENT("buttonCopy"), ui.selection!=NULL);
  gtk_widget_set_sensitive(GET_COMPONENT("buttonPaste"), ui.cur_item_type!=ITEM_TEXT);
}

void update_mapping_linkings(int toolno)
{
  int i;
  
  for (i = 1; i<=NUM_BUTTONS; i++) {
    if (ui.linked_brush[i] == BRUSH_LINKED) {
      if (toolno >= 0 && toolno < NUM_STROKE_TOOLS)
        g_memmove(&(ui.brushes[i][toolno]), &(ui.brushes[0][toolno]), sizeof(struct Brush));
    }
    if (ui.linked_brush[i] == BRUSH_COPIED && toolno == ui.toolno[i]) {
      ui.linked_brush[i] = BRUSH_STATIC;
      if (i==1 || i==2) update_mappings_menu_linkings();
    }
  }
}

void set_cur_color(int color_no, guint color_rgba)
{
  int which_mapping, tool;
  
  if (ui.toolno[ui.cur_mapping] == TOOL_HIGHLIGHTER) tool = TOOL_HIGHLIGHTER;
  else tool = TOOL_PEN;
  if (ui.cur_mapping>0 && ui.linked_brush[ui.cur_mapping]!=BRUSH_LINKED)
    which_mapping = ui.cur_mapping;
  else which_mapping = 0;

  ui.brushes[which_mapping][tool].color_no = color_no;
  if (tool == TOOL_HIGHLIGHTER && (color_rgba & 0xff) == 0xff)
    ui.brushes[which_mapping][tool].color_rgba = color_rgba & ui.hiliter_alpha_mask;
  else
    ui.brushes[which_mapping][tool].color_rgba = color_rgba;
  update_mapping_linkings(tool);
}

void recolor_temp_text(int color_no, guint color_rgba)
{
  GdkColor gdkcolor;
  
  if (ui.cur_item_type!=ITEM_TEXT) return;
  if (ui.cur_item->text!=NULL && ui.cur_item->brush.color_rgba != color_rgba) {
    prepare_new_undo();
    undo->type = ITEM_TEXT_ATTRIB;
    undo->item = ui.cur_item;
    undo->str = g_strdup(ui.cur_item->font_name);
    undo->val_x = ui.cur_item->font_size;
    undo->brush = (struct Brush *)g_memdup(&(ui.cur_item->brush), sizeof(struct Brush));
  }
  ui.cur_item->brush.color_no = color_no;
  ui.cur_item->brush.color_rgba = color_rgba;
  xo_rgb_to_GdkColor(color_rgba, &gdkcolor);
  gtk_widget_modify_text(ui.cur_item->widget, GTK_STATE_NORMAL, &gdkcolor);
  gtk_widget_grab_focus(ui.cur_item->widget);
}

void process_color_activate(GtkMenuItem *menuitem, int color_no, guint color_rgba)
{
  TRACE_3("enter color color index [%d] color [%x]\n", color_no, color_rgba);
  if (G_OBJECT_TYPE(menuitem) == GTK_TYPE_RADIO_MENU_ITEM) {
    if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM (menuitem)))
      return;
  } 
  else if (G_OBJECT_TYPE(menuitem) == GTK_TYPE_RADIO_TOOL_BUTTON) {
    if (!gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON (menuitem)))
      return;
  }

  if (ui.cur_mapping != 0 && !ui.button_switch_mapping) 
    return; // not user-generated

  if (ui.cur_item_type == ITEM_TEXT)
    recolor_temp_text(color_no, color_rgba);

  if (ui.selection != NULL) {
    recolor_selection(color_no, color_rgba);
    update_color_buttons();
    update_color_menu();
  }
  
  if (ui.toolno[ui.cur_mapping] != TOOL_PEN && ui.toolno[ui.cur_mapping] != TOOL_HIGHLIGHTER
      && ui.toolno[ui.cur_mapping] != TOOL_TEXT) {
    if (ui.selection != NULL) return;
    ui.cur_mapping = 0;
    end_text();
    ui.toolno[ui.cur_mapping] = TOOL_PEN;
    ui.cur_brush = &(ui.brushes[ui.cur_mapping][TOOL_PEN]);
    update_tool_buttons();
    update_tool_menu();
  }
  
  set_cur_color(color_no, color_rgba);
  update_color_buttons();
  update_color_menu();
  update_cursor();
}

void process_thickness_activate(GtkMenuItem *menuitem, int tool, int val)
{
  int which_mapping;
  
  if (G_OBJECT_TYPE(menuitem) == GTK_TYPE_RADIO_MENU_ITEM) {
    if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM (menuitem)))
      return;
  } else {
    if (!gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON (menuitem)))
      return;
  }

  if (ui.cur_mapping != 0 && !ui.button_switch_mapping) return; // not user-generated

  if (ui.selection != NULL && G_OBJECT_TYPE(menuitem) != GTK_TYPE_RADIO_MENU_ITEM) {
    rethicken_selection(val);
    update_thickness_buttons();
  }

  if (tool >= NUM_STROKE_TOOLS) {
    update_thickness_buttons(); // undo illegal button selection
    return;
  }

  if (ui.cur_mapping>0 && ui.linked_brush[ui.cur_mapping]!=BRUSH_LINKED)
    which_mapping = ui.cur_mapping;
  else which_mapping = 0;
  if (ui.brushes[which_mapping][tool].thickness_no == val) return;
  end_text();
  ui.brushes[which_mapping][tool].thickness_no = val;
  ui.brushes[which_mapping][tool].thickness = predef_thickness[tool][val];
  update_mapping_linkings(tool);
  
  update_thickness_buttons();
  if (tool == TOOL_PEN) update_pen_props_menu();
  if (tool == TOOL_ERASER) update_eraser_props_menu();
  if (tool == TOOL_HIGHLIGHTER) update_highlighter_props_menu();
  update_cursor();
}

void process_papercolor_activate(GtkMenuItem *menuitem, int color, guint rgba)
{
  struct Page *pg;
  GList *pglist;
  gboolean hasdone;

  if (G_OBJECT_TYPE(menuitem) == GTK_TYPE_RADIO_MENU_ITEM) {
    if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM (menuitem)))
      return;
  }

  if ((ui.cur_page->bg->type != BG_SOLID) || ui.bg_apply_all_pages || color == COLOR_OTHER)
    gtk_check_menu_item_set_active(
      GTK_CHECK_MENU_ITEM(GET_COMPONENT("papercolorNA")), TRUE);

  pg = ui.cur_page;
  hasdone = FALSE;
  for (pglist = journal.pages; pglist!=NULL; pglist = pglist->next) {
    if (ui.bg_apply_all_pages) pg = (struct Page *)pglist->data;
    if (pg->bg->type == BG_SOLID && pg->bg->color_rgba != rgba) {
      prepare_new_undo();
      if (hasdone) undo->multiop |= MULTIOP_CONT_UNDO;
      undo->multiop |= MULTIOP_CONT_REDO;
      hasdone = TRUE;
      undo->type = ITEM_NEW_BG_ONE;
      undo->page = pg;
      undo->bg = (struct Background *)g_memdup(pg->bg, sizeof(struct Background));
      undo->bg->canvas_group = NULL;

      pg->bg->color_no = color;
      pg->bg->color_rgba = rgba;
      update_canvas_bg(pg);
    }
    if (!ui.bg_apply_all_pages) break;
  }
  if (hasdone) undo->multiop -= MULTIOP_CONT_REDO;
}

void process_paperstyle_activate(GtkMenuItem *menuitem, int style)
{
  struct Page *pg;
  GList *pglist;
  gboolean hasdone, must_upd;

  if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM (menuitem)))
    return;

  if (ui.bg_apply_all_pages)
    gtk_check_menu_item_set_active(
      GTK_CHECK_MENU_ITEM(GET_COMPONENT("paperstyleNA")), TRUE);

  pg = ui.cur_page;
  hasdone = FALSE;
  must_upd = FALSE;
  for (pglist = journal.pages; pglist!=NULL; pglist = pglist->next) {
    if (ui.bg_apply_all_pages) pg = (struct Page *)pglist->data;
    if (pg->bg->type != BG_SOLID || pg->bg->ruling != style) {
      prepare_new_undo();
      undo->type = ITEM_NEW_BG_ONE;
      if (hasdone) undo->multiop |= MULTIOP_CONT_UNDO;
      undo->multiop |= MULTIOP_CONT_REDO;
      hasdone = TRUE;
      undo->page = pg;
      undo->bg = (struct Background *)g_memdup(pg->bg, sizeof(struct Background));
      undo->bg->canvas_group = NULL;

      if (pg->bg->type != BG_SOLID) {
        pg->bg->type = BG_SOLID;
        pg->bg->color_no = COLOR_WHITE;
        pg->bg->color_rgba = predef_bgcolors_rgba[COLOR_WHITE];
        pg->bg->filename = NULL;
        pg->bg->pixbuf = NULL;
	pg->bg->canvas_pixbuf = NULL;
        must_upd = TRUE;
      }
      pg->bg->ruling = style;
      update_canvas_bg(pg);
    }
    if (!ui.bg_apply_all_pages) break;
  }
  if (hasdone) undo->multiop -= MULTIOP_CONT_REDO;
  if (must_upd) update_page_stuff();
}

#ifndef GTK_STOCK_DISCARD
#define GTK_STOCK_DISCARD GTK_STOCK_NO
#endif

gboolean ok_to_close(void)
{
  GtkWidget *dialog;
  GtkResponseType response;

  if (ui.saved) return TRUE;
  dialog = gtk_message_dialog_new(GTK_WINDOW (winMain), GTK_DIALOG_DESTROY_WITH_PARENT,
    GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, _("Save changes to '%s'?"),
    (ui.filename!=NULL) ? ui.filename:_("Untitled"));
  gtk_dialog_add_button(GTK_DIALOG (dialog), GTK_STOCK_DISCARD, GTK_RESPONSE_NO);
  gtk_dialog_add_button(GTK_DIALOG (dialog), GTK_STOCK_SAVE, GTK_RESPONSE_YES);
  gtk_dialog_add_button(GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
  gtk_dialog_set_default_response(GTK_DIALOG (dialog), GTK_RESPONSE_YES);
  response = gtk_dialog_run(GTK_DIALOG (dialog));
  gtk_widget_destroy(dialog);
  if (response == GTK_RESPONSE_CANCEL || response == GTK_RESPONSE_DELETE_EVENT) 
    return FALSE; // aborted
  if (response == GTK_RESPONSE_YES) {
    on_fileSave_activate(NULL, NULL);
    if (!ui.saved) return FALSE; // if save failed, then we abort
  }
  return TRUE;
}

// send the focus back to the appropriate widget

void reset_focus(void)
{
  if (ui.cur_item_type == ITEM_TEXT)
    gtk_widget_grab_focus(ui.cur_item->widget);
  else
    gtk_widget_grab_focus(GTK_WIDGET(canvas));
}

// selection / clipboard stuff

void reset_selection(void)
{
  if (ui.selection == NULL) 
    return;

  if (ui.selection->canvas_item != NULL)  {
    goo_canvas_item_remove(ui.selection->canvas_item);
  }
  g_list_free(ui.selection->items);
  g_free(ui.selection);
  ui.selection = NULL;
  update_copy_paste_enabled();
  update_color_menu();
  update_thickness_buttons();
  update_color_buttons();
  update_font_button();
  update_cursor();
}

void move_journal_items_by(GList *itemlist, double dx, double dy,
                              struct Layer *l1, struct Layer *l2, GList *depths)
{
  struct Item *item;
  GooCanvasItem *refitem;
  GList *link;
  int i;
  double *pt;
  
  while (itemlist!=NULL) {
    item = (struct Item *)itemlist->data;
    if (item->type == ITEM_STROKE)
      for (pt=item->path->coords, i=0; i<item->path->num_points; i++, pt+=2)
        { pt[0] += dx; pt[1] += dy; }
    if (item->type == ITEM_STROKE || item->type == ITEM_TEXT || 
        item->type == ITEM_TEMP_TEXT || item->type == ITEM_IMAGE) {
      item->bbox.left += dx;
      item->bbox.right += dx;
      item->bbox.top += dy;
      item->bbox.bottom += dy;
    }
    if (l1 != l2) {
      // find out where to insert
      if (depths != NULL) {
        if (depths->data == NULL) link = l2->items;
        else {
          link = g_list_find(l2->items, depths->data);
          if (link != NULL) link = link->next;
        }
      } else link = NULL;
      l2->items = g_list_insert_before(l2->items, link, item);
      l2->nitems++;
      l1->items = g_list_remove(l1->items, item);
      l1->nitems--;
    }
    if (depths != NULL) { // also raise/lower the canvas items
      if (item->canvas_item!=NULL) {
        if (depths->data == NULL) link = NULL;
        else link = g_list_find(l2->items, depths->data);
        if (link != NULL) refitem = ((struct Item *)(link->data))->canvas_item;
        else refitem = NULL;
        goo_canvas_item_lower(item->canvas_item, refitem);
      }
      depths = depths->next;
    }
    itemlist = itemlist->next;
  }
}

void resize_journal_items_by(GList *itemlist, double scaling_x, double scaling_y,
                             double offset_x, double offset_y)
{
  struct Item *item;
  GList *list;
  double mean_scaling, temp;
  double *pt, *wid;
  GooCanvasItem *group;
  int i; 


  
  /* geometric mean of x and y scalings = rescaling for stroke widths
     and for text font sizes */
  mean_scaling = sqrt(fabs(scaling_x * scaling_y));

  for (list = itemlist; list != NULL; list = list->next) {
    item = (struct Item *)list->data;
    if (item->type == ITEM_STROKE) {
      item->brush.thickness = item->brush.thickness * mean_scaling;
      for (i=0, pt=item->path->coords; i<item->path->num_points; i++, pt+=2) {
        pt[0] = pt[0]*scaling_x + offset_x;
        pt[1] = pt[1]*scaling_y + offset_y;
      }
      if (item->brush.variable_width)
        for (i=0, wid=item->widths; i<item->path->num_points-1; i++, wid++)
          *wid = *wid * mean_scaling;

      item->bbox.left = item->bbox.left*scaling_x + offset_x;
      item->bbox.right = item->bbox.right*scaling_x + offset_x;
      item->bbox.top = item->bbox.top*scaling_y + offset_y;
      item->bbox.bottom = item->bbox.bottom*scaling_y + offset_y;
      if (item->bbox.left > item->bbox.right) {
        temp = item->bbox.left;
        item->bbox.left = item->bbox.right;
        item->bbox.right = temp;
      }
      if (item->bbox.top > item->bbox.bottom) {
        temp = item->bbox.top;
        item->bbox.top = item->bbox.bottom;
        item->bbox.bottom = temp;
      }
    }
    if (item->type == ITEM_TEXT) {
      /* must scale about NW corner -- all other points of the text box
         are font- and zoom-dependent, so scaling about center of text box
         couldn't be undone properly. FIXME? */
      item->font_size *= mean_scaling;
      item->bbox.left = item->bbox.left*scaling_x + offset_x;
      item->bbox.top = item->bbox.top*scaling_y + offset_y;
    }
    if (item->type == ITEM_IMAGE) {
      item->bbox.left = item->bbox.left*scaling_x + offset_x;
      item->bbox.right = item->bbox.right*scaling_x + offset_x;
      item->bbox.top = item->bbox.top*scaling_y + offset_y;
      item->bbox.bottom = item->bbox.bottom*scaling_y + offset_y;
      if (item->bbox.left > item->bbox.right) {
        temp = item->bbox.left;
        item->bbox.left = item->bbox.right;
        item->bbox.right = temp;
      }
      if (item->bbox.top > item->bbox.bottom) {
        temp = item->bbox.top;
        item->bbox.top = item->bbox.bottom;
        item->bbox.bottom = temp;
      }
    }
    // redraw the item
    if (item->canvas_item!=NULL) {
      group =  goo_canvas_item_get_parent(item->canvas_item);
      // delete it
      goo_canvas_item_remove(item->canvas_item);
      //create it again

      make_canvas_item_one(group, item);
    }
  }
}

// Switch between button mappings

/* NOTE ABOUT BUTTON MAPPINGS: ui.cur_mapping is 0 except while a canvas
   click event is being processed ... or if ui.button_switch_mapping is
   enabled and mappings are switched (but even then, canvas should have
   a pointer grab from the initial click that switched the mapping) */

void switch_mapping(int m)
{
  if (ui.cur_mapping == m) return;

  ui.cur_mapping = m;
  if (ui.toolno[m] < NUM_STROKE_TOOLS) 
    ui.cur_brush = &(ui.brushes[m][ui.toolno[m]]);
  if (ui.toolno[m] == TOOL_TEXT)
    ui.cur_brush = &(ui.brushes[m][TOOL_PEN]);
  if (m==0) ui.which_unswitch_button = 0;
  
  update_tool_buttons();
  update_color_menu();
  update_cursor();
}

void process_mapping_activate(GtkMenuItem *menuitem, int m, int tool)
{
  if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(menuitem))) return;
  if (ui.cur_mapping!=0 && !ui.button_switch_mapping) return;
  if (ui.toolno[m] == tool) return;
  switch_mapping(0);
  end_text();
    
  ui.toolno[m] = tool;
  if (ui.linked_brush[m] == BRUSH_COPIED) {
    ui.linked_brush[m] = BRUSH_STATIC;
    update_mappings_menu_linkings();
  }
}

// update the ordering of components in the main vbox

const char *vbox_component_names[VBOX_MAIN_NITEMS]=
 {"scrolledwindowMain", "menubar", "toolbarMain", "toolbarPen", "hbox1"};

void update_vbox_order(int *order)
{
  int i, j;
  GtkWidget *child;
  GtkBox *vboxMain = GTK_BOX(GET_COMPONENT("vboxMain"));
  gboolean present[VBOX_MAIN_NITEMS];
  
  for (i=0; i<VBOX_MAIN_NITEMS; i++) present[i] = FALSE;
  j=0;
  for (i=0; i<VBOX_MAIN_NITEMS; i++) {
    if (order[i]<0 || order[i]>=VBOX_MAIN_NITEMS) continue;
    present[order[i]] = TRUE;
    child = GET_COMPONENT(vbox_component_names[order[i]]);
    gtk_box_reorder_child(vboxMain, child, j++);
    gtk_widget_show(child);
  }
  for (i=1; i<VBOX_MAIN_NITEMS; i++) // hide others, but not the drawing area!
    if (!present[i]) gtk_widget_hide(GET_COMPONENT(vbox_component_names[i]));
}

gchar *make_cur_font_name(void)
{
  gchar *str;
  struct Item *it;

  if (ui.cur_item_type == ITEM_TEXT)
    str = g_strdup_printf("%s %.1f", ui.cur_item->font_name, ui.cur_item->font_size);
  else if (ui.selection!=NULL && ui.selection->items!=NULL &&
           ui.selection->items->next==NULL &&
           (it=(struct Item*)ui.selection->items->data)->type == ITEM_TEXT)
    str = g_strdup_printf("%s %.1f", it->font_name, it->font_size);
  else
    str = g_strdup_printf("%s %.1f", ui.font_name, ui.font_size);
  return str;
}

void update_font_button(void)
{
  gchar *str;

  str = make_cur_font_name();
  gtk_font_button_set_font_name(GTK_FONT_BUTTON(GET_COMPONENT("fontButton")), str);
  g_free(str);
}

gboolean can_accel(GtkWidget *widget, guint id, gpointer data)
{
  return  gtk_widget_get_sensitive(widget);
}

gboolean can_accel_except_text(GtkWidget *widget, guint id, gpointer data)
{
  if (ui.cur_item_type == ITEM_TEXT) {
    g_signal_stop_emission_by_name(widget, "can-activate-accel");
    return FALSE;
  }
  return  gtk_widget_get_sensitive(widget);
}

void allow_all_accels(void)
{
  g_signal_connect((gpointer) GET_COMPONENT("fileNew"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("fileOpen"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("fileSave"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("filePrint"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("filePrintPDF"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("fileQuit"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("editUndo"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("editRedo"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("editCut"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("editCopy"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("editPaste"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("editDelete"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("viewFullscreen"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("viewZoomIn"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("viewZoomOut"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("viewNormalSize"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("viewPageWidth"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("viewFirstPage"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("viewPreviousPage"),
      "can-activate-accel", G_CALLBACK(can_accel_except_text), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("viewNextPage"),
      "can-activate-accel", G_CALLBACK(can_accel_except_text), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("viewLastPage"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("toolsPen"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("toolsEraser"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("toolsHighlighter"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("toolsText"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("toolsSelectRegion"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("toolsSelectRectangle"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("toolsVerticalSpace"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("toolsHand"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("toolsTextFont"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("toolsRuler"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
  g_signal_connect((gpointer) GET_COMPONENT("toolsReco"),
      "can-activate-accel", G_CALLBACK(can_accel), NULL);
}

void add_scroll_bindings(void)
{
  GtkBindingSet *binding_set;
  
  binding_set = gtk_binding_set_by_class(
     G_OBJECT_GET_CLASS(GET_COMPONENT("scrolledwindowMain")));
  gtk_binding_entry_add_signal(binding_set, GDK_Up, 0,
    "scroll_child", 2, GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_STEP_BACKWARD, 
    G_TYPE_BOOLEAN, FALSE);  
  gtk_binding_entry_add_signal(binding_set, GDK_KP_Up, 0,
    "scroll_child", 2, GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_STEP_BACKWARD, 
    G_TYPE_BOOLEAN, FALSE);  
  gtk_binding_entry_add_signal(binding_set, GDK_Down, 0,
    "scroll_child", 2, GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_STEP_FORWARD, 
    G_TYPE_BOOLEAN, FALSE);  
  gtk_binding_entry_add_signal(binding_set, GDK_KP_Down, 0,
    "scroll_child", 2, GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_STEP_FORWARD, 
    G_TYPE_BOOLEAN, FALSE);  
  gtk_binding_entry_add_signal(binding_set, GDK_Left, 0,
    "scroll_child", 2, GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_STEP_BACKWARD, 
    G_TYPE_BOOLEAN, TRUE);  
  gtk_binding_entry_add_signal(binding_set, GDK_KP_Left, 0,
    "scroll_child", 2, GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_STEP_BACKWARD, 
    G_TYPE_BOOLEAN, TRUE);  
  gtk_binding_entry_add_signal(binding_set, GDK_Right, 0,
    "scroll_child", 2, GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_STEP_FORWARD, 
    G_TYPE_BOOLEAN, TRUE);  
  gtk_binding_entry_add_signal(binding_set, GDK_KP_Right, 0,
    "scroll_child", 2, GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_STEP_FORWARD, 
    G_TYPE_BOOLEAN, TRUE);  
}

gboolean is_event_within_textview(GdkEventButton *event)
{
  double pt[2];
  
  if (ui.cur_item_type!=ITEM_TEXT) return FALSE;
  xo_event_get_pointer_coords((GdkEvent *)event, pt);
  if (pt[0]<ui.cur_item->bbox.left || pt[0]>ui.cur_item->bbox.right) return FALSE;
  if (pt[1]<ui.cur_item->bbox.top || pt[1]>ui.cur_item->bbox.bottom) return FALSE;
  return TRUE;
}

void hide_unimplemented(void)
{
  gtk_widget_hide(GET_COMPONENT("filePrintOptions"));
  gtk_widget_hide(GET_COMPONENT("journalFlatten"));  
  gtk_widget_hide(GET_COMPONENT("helpIndex")); 

  /* config file only works with glib 2.6 and beyond */
  if (glib_minor_version<6) {
    gtk_widget_hide(GET_COMPONENT("optionsAutoSavePrefs"));
    gtk_widget_hide(GET_COMPONENT("optionsSavePreferences"));
  }
  
  /* screenshot feature doesn't work yet in Win32 */
#ifdef WIN32
  gtk_widget_hide(GET_COMPONENT("journalScreenshot"));
#endif
}  

// toggle fullscreen mode
void do_fullscreen(gboolean active)
{
  end_text();
  ui.fullscreen = active;
  gtk_check_menu_item_set_active(
    GTK_CHECK_MENU_ITEM(GET_COMPONENT("viewFullscreen")), ui.fullscreen);
  gtk_toggle_tool_button_set_active(
    GTK_TOGGLE_TOOL_BUTTON(GET_COMPONENT("buttonFullscreen")), ui.fullscreen);

  if (ui.fullscreen) {
#ifdef WIN32
    gtk_window_get_size(GTK_WINDOW(winMain), &ui.pre_fullscreen_width, &ui.pre_fullscreen_height);
    gtk_widget_set_size_request(GTK_WIDGET(winMain), gdk_screen_width(),
                                                     gdk_screen_height());
#endif
    gtk_window_fullscreen(GTK_WINDOW(winMain));
  }
  else {
#ifdef WIN32
    gtk_widget_set_size_request(GTK_WIDGET(winMain), -1, -1);
    gtk_window_resize(GTK_WINDOW(winMain), ui.pre_fullscreen_width,
                                           ui.pre_fullscreen_height);
#endif
    gtk_window_unfullscreen(GTK_WINDOW(winMain));
  }

  update_vbox_order(ui.vertical_order[ui.fullscreen?1:0]);
}

/* attempt to work around GTK+ 2.16/2.17 bugs where random interface
   elements receive XInput events that they can't handle properly    */

// prevent interface items from getting bogus XInput events

gboolean filter_extended_events (GtkWidget *widget, GdkEventMotion *event,
                                   gpointer user_data)
{
  gboolean eventIsCore = xo_event_motion_device_is_core(event);

  if (event->type == GDK_MOTION_NOTIFY &&
      //      event->motion.device != gdk_device_get_core_pointer())
      ! eventIsCore)
    return TRUE;
  if ((event->type == GDK_BUTTON_PRESS || event->type == GDK_2BUTTON_PRESS ||
      event->type == GDK_3BUTTON_PRESS || event->type == GDK_BUTTON_RELEASE) &&
      //      event->button.device != gdk_device_get_core_pointer())
      ! eventIsCore)
    return TRUE;
  return FALSE;
}

/* Code to turn an extended input event into a core event and send it to
   a different GdkWindow -- e.g. could be used when a click in a text edit box
   gets sent to the canvas instead due to incorrect event translation.
   We now turn off xinput altogether while editing text under GTK+ 2.17, so
   this isn't needed any more... but could become useful again someday!
*/

/*  
gboolean fix_extended_events (GtkWidget *widget, GdkEvent *event,
                                   gpointer user_data)
{
  int ix, iy;
  GdkWindow *window;

  if (user_data) window = (GdkWindow *)user_data;
  else window = widget->window;

  if (event->type == GDK_MOTION_NOTIFY &&
      event->motion.device != gdk_device_get_core_pointer()) {
//    printf("fixing motion\n");
    gdk_window_get_pointer(window, &ix, &iy, NULL);
    event->motion.x = ix; event->motion.y = iy;
    event->motion.device = gdk_device_get_core_pointer();
    g_object_unref(event->motion.window);
    event->motion.window = g_object_ref(window);
    gtk_widget_event(widget, event);
    return TRUE;
  }
  if ((event->type == GDK_BUTTON_PRESS || event->type == GDK_BUTTON_RELEASE) &&
      event->button.device != gdk_device_get_core_pointer()) {
//    printf("fixing button from pos = %f, %f\n", event->button.x, event->button.y);
    gdk_window_get_pointer(window, &ix, &iy, NULL);
    event->button.x = ix; event->button.y = iy;
    event->button.device = gdk_device_get_core_pointer();
    g_object_unref(event->button.window);
    event->button.window = g_object_ref(window);
//    printf("fixing button to pos = %f, %f\n", event->button.x, event->button.y);
    gtk_widget_event(widget, event);
    return TRUE;
  }
  return FALSE;
}
*/


/* When enter is pressed into page spinbox, send focus back to canvas. */

gboolean handle_activate_signal(GtkWidget *widget, gpointer user_data)
{
  reset_focus();
  return FALSE;
}

/* recursively unset widget flags */

void xo_unset_focus(GtkWidget *w, gpointer unused)
{
  gtk_widget_set_can_focus (w, FALSE);
  if(GTK_IS_CONTAINER(w))
    gtk_container_forall(GTK_CONTAINER(w), xo_unset_focus, NULL);
}

/* recursively unset widget flags */

void unset_flags(GtkWidget *w, gpointer flag)
{
#ifdef ABC
  GTK_WIDGET_UNSET_FLAGS(w, (GtkWidgetFlags)flag);
  if(GTK_IS_CONTAINER(w))
    gtk_container_forall(GTK_CONTAINER(w), unset_flags, flag);
#endif
  assert(0);
}

/* reset focus when a key or button press event reaches someone, or when the
   page-number spin button should relinquish control... */

gboolean intercept_activate_events(GtkWidget *w, GdkEvent *ev, gpointer data)
{
  if (w == GET_COMPONENT("hbox1")) {
    /* the event won't be processed since the hbox1 doesn't know what to do with it,
       so we might as well kill it and avoid confusing ourselves when it gets
       propagated further ... */
    return TRUE;
  }
  if (w == GET_COMPONENT("spinPageNo")) {
    /* we let the spin button take care of itself, and don't steal its focus,
       unless the user presses Esc or Tab (in those cases we intervene) */
    if (ev->type != GDK_KEY_PRESS) return FALSE;
    if (ev->key.keyval == GDK_Escape) 
       gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), ui.pageno+1); // abort
    else if (ev->key.keyval != GDK_Tab && ev->key.keyval != GDK_ISO_Left_Tab)
       return FALSE; // let the spin button process it
  }

  // otherwise, we want to make sure the canvas or text item gets focus back...
  reset_focus();  
  return FALSE;
}

void install_focus_hooks(GtkWidget *w, gpointer data)
{
  if (w == NULL) return;
  g_signal_connect(w, "key-press-event", G_CALLBACK(intercept_activate_events), data);
  g_signal_connect(w, "button-press-event", G_CALLBACK(intercept_activate_events), data);
  if (GTK_IS_MENU_ITEM(w)) {
    g_signal_connect(w, "activate", G_CALLBACK(intercept_activate_events), data);
    install_focus_hooks(gtk_menu_item_get_submenu(GTK_MENU_ITEM(w)), data);
  }
  if(GTK_IS_CONTAINER(w))
    gtk_container_forall(GTK_CONTAINER(w), install_focus_hooks, data);
}

// wrapper for missing poppler functions (defunct poppler-gdk api)

static void
xo_wrapper_copy_cairo_surface_to_pixbuf (cairo_surface_t *surface,
					 GdkPixbuf       *pixbuf)
{
  int cairo_width, cairo_height, cairo_rowstride;
  unsigned char *pixbuf_data, *dst, *cairo_data;
  int pixbuf_rowstride, pixbuf_n_channels;
  unsigned int *src;
  int x, y;

  cairo_width = cairo_image_surface_get_width (surface);
  cairo_height = cairo_image_surface_get_height (surface);
  cairo_rowstride = cairo_image_surface_get_stride (surface);
  cairo_data = cairo_image_surface_get_data (surface);

  pixbuf_data = gdk_pixbuf_get_pixels (pixbuf);
  pixbuf_rowstride = gdk_pixbuf_get_rowstride (pixbuf);
  pixbuf_n_channels = gdk_pixbuf_get_n_channels (pixbuf);

  if (cairo_width > gdk_pixbuf_get_width (pixbuf))
    cairo_width = gdk_pixbuf_get_width (pixbuf);
  if (cairo_height > gdk_pixbuf_get_height (pixbuf))
    cairo_height = gdk_pixbuf_get_height (pixbuf);
  for (y = 0; y < cairo_height; y++)
    {
      src = (unsigned int *) (cairo_data + y * cairo_rowstride);
      dst = pixbuf_data + y * pixbuf_rowstride;
      for (x = 0; x < cairo_width; x++) 
	{
	  dst[0] = (*src >> 16) & 0xff;
	  dst[1] = (*src >> 8) & 0xff; 
	  dst[2] = (*src >> 0) & 0xff;
	  if (pixbuf_n_channels == 4)
	      dst[3] = (*src >> 24) & 0xff;
	  dst += pixbuf_n_channels;
	  src++;
	}
    }
}	


GdkPixbuf* xo_wrapper_poppler_page_render_to_pixbuf (PopplerPage *page,
						     int src_x, int src_y,
						     int src_width, int src_height,
						     double scale, 
						     int rotation)
{
  cairo_t *cr;
  cairo_surface_t *surface;
  GdkPixbuf *pixbuf;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
					src_width, src_height);
  cr = cairo_create (surface);
  cairo_set_source_rgb(cr, 1., 1., 1.);
  cairo_paint (cr);

  switch (rotation) {
  case 90:
	  cairo_translate (cr, src_x + src_width, -src_y);
	  break;
  case 180:
	  cairo_translate (cr, src_x + src_width, src_y + src_height);
	  break;
  case 270:
	  cairo_translate (cr, -src_x, src_y + src_height);
	  break;
  default:
	  cairo_translate (cr, -src_x, -src_y);
  }
  if (scale != 1.0)
	  cairo_scale (cr, scale, scale);
  if (rotation != 0)
	  cairo_rotate (cr, rotation * G_PI / 180.0);

  poppler_page_render (page, cr);

  cairo_destroy (cr);

  if (ui.poppler_force_cairo) {
    GError *error;

    pixbuf = gdk_pixbuf_get_from_surface (surface,
					  0,0,
					  src_width, src_height);
  } else  {
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB,
			  FALSE, 8, src_width, src_height);
    xo_wrapper_copy_cairo_surface_to_pixbuf (surface, pixbuf);
  }

  cairo_surface_destroy (surface);
  return pixbuf;
}


GdkDeviceManager *xo_device_manager_get(GdkWindow *window)
{
  GdkDeviceManager *manager;
  assert(window != NULL);
  manager = gdk_display_get_device_manager(gdk_window_get_display(window));
  return manager;
}

GList *xo_gdkwindow_devices_list(GdkWindow *window)
{
  assert(window != NULL);
  return gdk_device_manager_list_devices(xo_device_manager_get(window), GDK_DEVICE_TYPE_MASTER);
}

GList *xo_devices_list(GtkWidget *w)
{
  GdkWindow *window;
  if (w == NULL) 
    window = gdk_get_default_root_window ();
  else
    window = gtk_widget_get_window(w);

  assert(window != NULL);
  return gdk_device_manager_list_devices(xo_device_manager_get(window), GDK_DEVICE_TYPE_MASTER);
}



gboolean xo_gtkwidget_device_is_core(GtkWidget *w, GdkDevice *device)
{
  GdkWindow *window;

  if (w == NULL)
    window = gdk_get_default_root_window ();
  else
    window = gtk_widget_get_parent_window(w);
  assert(window != NULL);

  return gdk_device_manager_get_client_pointer(xo_device_manager_get(window)) != device;
}

gboolean xo_event_motion_device_is_core(GdkEventMotion  *event)
{
  return event->device == 
    gdk_device_manager_get_client_pointer(xo_device_manager_get(event->window));
}

gboolean xo_event_button_device_is_core(GdkEventButton  *event)
{
  return event->device == 
    gdk_device_manager_get_client_pointer(xo_device_manager_get(event->window));
}


void xo_canvas_set_pixels_per_unit(void)
{

#ifdef ABC
  gnome_canvas_set_pixels_per_unit(canvas, ui.zoom);
#else
  if (fabs(goo_canvas_get_scale(canvas) - ui.zoom) > 0.02) {
    goo_canvas_set_scale(canvas, ui.zoom);
  }
#endif
}

void xo_canvas_get_scroll_offsets_in_world(GooCanvas *canvas, gdouble *x, gdouble *y) 
{
  GtkAdjustment *v_adj, *h_adj;

  //the return values can be negative if the canvas is to the right, lower corner
  // of its container
  
  v_adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(canvas));
  h_adj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(canvas));

  *x = gtk_adjustment_get_value(h_adj);
  *y = gtk_adjustment_get_value(v_adj);
 
  goo_canvas_convert_from_pixels(canvas, x, y);

}

void xo_canvas_get_scroll_offsets_in_pixels(GooCanvas *canvas, gdouble *x, gdouble *y) 
{
  GtkAdjustment *v_adj, *h_adj;

  //the return values can be negative if the canvas is to the right, lower corner
  // of its container
  
  v_adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(canvas));
  h_adj = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(canvas));

  *x = gtk_adjustment_get_value(h_adj);
  *y = gtk_adjustment_get_value(v_adj);
 
}


gboolean xo_dialog_select_color(gchar *title, guint32 *rgbaColor, gboolean ignoreAlpha)
{
  GdkRGBA gdkcolor;
  gboolean isOk;
  GtkWidget *dialog;

  dialog = gtk_color_chooser_dialog_new (title, NULL);
  xo_rgba_to_GdkRGBA(*rgbaColor, &gdkcolor);
  gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (dialog), &gdkcolor);
  
  isOk = (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK);
  if (isOk) {
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (dialog), &gdkcolor);
    *rgbaColor = xo_GdkRGBA_to_rgba(&gdkcolor);
    if (ignoreAlpha) {
      *rgbaColor &= 0xFFFFFFFF;
    }
  } else {
    ;
  }
  gtk_widget_destroy(dialog);
  return isOk;
}

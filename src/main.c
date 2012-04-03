/* Skeltrack Desktop Control: Main
 *
 * Copyright (c) 2012 Igalia, S.L.
 *
 * Author: Joaquim Rocha <jrocha@igalia.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gfreenect.h>
#include <skeltrack.h>
#include <math.h>
#include <string.h>
#include <glib-object.h>
#include <clutter/clutter.h>
#include <clutter/clutter-keysyms.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

static SkeltrackSkeleton *skeleton = NULL;
static GFreenectDevice *kinect = NULL;
static ClutterActor *info_text;
static ClutterActor *depth_tex;
static SkeltrackJointList list = NULL;
static gboolean SHOW_SKELETON = TRUE;

static Display *display = NULL;
static gint screen_width = 0;
static gint screen_height = 0;

/* In the Z axis, from the head*/
static gint GESTURE_THRESHOLD = 250;

/* Timeout after a hand gets ready to be interpreted
   and it actually is. In milliseconds. */
static guint GESTURE_TIMEOUT = 300;

static guint THRESHOLD_BEGIN = 500;
/* Adjust this value to increase of decrease
   the threshold */
static guint THRESHOLD_END   = 1500;

static ClutterEventType pointer_1_event_type = CLUTTER_NOTHING;
static ClutterEventType pointer_2_event_type = CLUTTER_NOTHING;

/* Affect how two hands gestures should be interpreted */
static gboolean DOUBLE_HAND_WHEEL_MODE = TRUE;

/* Distance between the two points (in 640x480)
   so that it should be considered as "steering wheel
   turned" gesture */
static guint WHEEL_TURN_ACTIVATE_DISTANCE = 35;

/* Distance between the two points (in 640x480)
   so that it should be considered a pinch gesture */
static guint PINCH_ACTIVATE_DISTANCE = 75;

typedef struct _Point Point;

static Point *pointer_1 = NULL;
static Point *pointer_2 = NULL;
static Point *last_left_point = NULL;
static Point *last_right_point = NULL;
static gint64 pointer_enter_time = 0;
static gint old_distance = -1;
static guint last_key = 0;

typedef struct
{
  guint16 *reduced_buffer;
  guint16 *original_buffer;
  gint width;
  gint height;
  gint reduced_width;
  gint reduced_height;
} BufferInfo;

struct _Point
{
  gint x;
  gint y;
  gint z;
};

static Point *
smooth_point (guint16 *buffer,
              guint width,
              guint height,
              SkeltrackJoint *joint);

static gint
get_distance (Point *point_a, Point *point_b)
{
  gint dx, dy;
  dx = ABS (point_a->x - point_b->x);
  dy = ABS (point_a->y - point_b->y);
  return sqrt (dx * dx + dy * dy);
}

static void
get_pointer_position (Display *display, gint *x, gint *y)
{
  XEvent e;
  Window root;
  root = XRootWindow(display, 0);
  XQueryPointer(display, root,
                &e.xbutton.root, &e.xbutton.window,
                &e.xbutton.x_root, &e.xbutton.y_root,
                &e.xbutton.x, &e.xbutton.y,
                &e.xbutton.state);
  *x = e.xbutton.x_root;
  *y = e.xbutton.y_root;
}

static void
set_mouse_pointer (gint x, gint y)
{
  gint pos_x, pos_y;
  gdouble rel_x, rel_y;
  if (display == NULL)
    return;

  get_pointer_position (display, &pos_x, &pos_y);

  rel_x = screen_width - (x * screen_width / 640.f * 1.1);
  rel_y = y * screen_height / 480.f * 1.1;

  pos_x += round ((rel_x - pos_x) / 8.f);
  pos_y += round ((rel_y - pos_y) / 8.f);

  XTestFakeMotionEvent(display, -1, pos_x, pos_y, CurrentTime);
  XSync(display, 0);
}

static void
key_down (guint keycode)
{
  XTestFakeKeyEvent (display,
                     keycode,
                     TRUE,
                     CurrentTime);
  XSync(display, 0);
}

static void
key_up (guint keycode)
{
  XTestFakeKeyEvent (display,
                     keycode,
                     FALSE,
                     CurrentTime);
  XSync(display, 0);
}

static void
mouse_down (guint button)
{
  XTestFakeButtonEvent(display, button, TRUE, CurrentTime);
  XSync(display, 0);
}

static void
mouse_up (guint button)
{
  XTestFakeButtonEvent(display, button, FALSE, CurrentTime);
  XSync(display, 0);
}

static void
mouse_click (guint button)
{
  XTestFakeButtonEvent(display, button, TRUE, CurrentTime);
  XTestFakeButtonEvent(display, button, FALSE, CurrentTime);
  XSync(display, 0);
}

static gboolean
hand_is_active (SkeltrackJoint *head, SkeltrackJoint *hand)
{
  return hand != NULL && ABS (head->z - hand->z) > GESTURE_THRESHOLD;
}

static void
both_hands_left (void)
{
  XTestFakeButtonEvent(display, 1, FALSE, CurrentTime);
  XSync(display, 0);
  pointer_1 = NULL;
  pointer_2 = NULL;
  pointer_1_event_type = CLUTTER_NOTHING;
  pointer_2_event_type = CLUTTER_NOTHING;
  old_distance = -1;

  if (last_key != 0)
    {
      guint keycode;
      XTestFakeKeyEvent (display,
                         last_key,
                         FALSE,
                         CurrentTime);
      keycode = XKeysymToKeycode(display, XK_Up);
      XTestFakeKeyEvent (display,
                         keycode,
                         FALSE,
                         CurrentTime);
      XSync(display, 0);
      last_key = 0;
    }
}

static void
interpret_wheel_gesture (Point *pointer_1,
                         Point *pointer_2)
{
  /* Assuming pointer_1 is the left hand
     and pointer_2 is the right hand */
  guint keycode;

  if (pointer_1->y < pointer_2->y)
    {
      keycode = XKeysymToKeycode(display, XK_Right);
      g_debug ("RIGHT");
    }
  else
    {
      keycode = XKeysymToKeycode(display, XK_Left);
      g_debug ("LEFT");
    }

  if (last_key != 0 && keycode != last_key)
    {
      key_up (last_key);
    }
  if (ABS (pointer_1->y - pointer_2->y) / WHEEL_TURN_ACTIVATE_DISTANCE != 0)
    {
      key_down (keycode);
    }
  else
    {
      key_up (keycode);
    }
  keycode = XKeysymToKeycode(display, XK_Up);
  key_down (keycode);
  last_key = keycode;
}

static void
interpret_pinch_gesture (Point *pointer_1,
                         Point *pointer_2)
{
  if (old_distance == -1)
    {
      old_distance = get_distance (pointer_1, pointer_2);
    }
  else
    {
      gint new_distance = get_distance (pointer_1, pointer_2);
      if (ABS (old_distance - new_distance) > PINCH_ACTIVATE_DISTANCE)
        {
          g_debug ("ENTERED");
          guint keycode;
          keycode = XKeysymToKeycode(display, XK_Control_L);
          key_down (keycode);
          if (old_distance < new_distance)
            {
              mouse_click (4);
              g_debug ("SCROLL UP!");
            }
          else
            {
              mouse_click (5);
              g_debug ("SCROLL DOWN!");
            }
          key_up (keycode);
          old_distance = new_distance;
        }
    }
}

static void
interpret_guestures (SkeltrackJointList joint_list,
                     guint16 *buffer,
                     guint width,
                     guint height)
{
  gint64 time;
  Point *left_point, *right_point, *single_point;
  SkeltrackJoint *head, *left_hand, *right_hand;

  if (joint_list == NULL)
    return;

  head = skeltrack_joint_list_get_joint (joint_list,
                                         SKELTRACK_JOINT_ID_HEAD);
  left_hand = skeltrack_joint_list_get_joint (joint_list,
                                              SKELTRACK_JOINT_ID_LEFT_HAND);
  right_hand = skeltrack_joint_list_get_joint (joint_list,
                                               SKELTRACK_JOINT_ID_RIGHT_HAND);

  if (head == NULL)
    return;

  left_point = NULL;
  right_point = NULL;
  single_point = NULL;

  if (hand_is_active (head, left_hand))
    {
      left_point = smooth_point (buffer, width, height, left_hand);
      single_point = left_point;
      if (hand_is_active (head, right_hand))
        {
          right_point = smooth_point (buffer, width, height, right_hand);
          single_point = NULL;
        }
    }
  else if (hand_is_active (head, right_hand))
    {
      right_point = smooth_point (buffer, width, height, right_hand);
      single_point = right_point;
    }

  if (single_point)
    {
      if (last_key != 0)
        {
          key_up (last_key);
          last_key = 0;
        }

      if (pointer_1_event_type == CLUTTER_SCROLL)
        {
          pointer_1_event_type = CLUTTER_NOTHING;
        }
      else if (pointer_2_event_type == CLUTTER_BUTTON_PRESS)
        {
          mouse_up (1);
        }
      else if (pointer_2_event_type == CLUTTER_ENTER)
        {
          mouse_click (1);
          pointer_2_event_type = CLUTTER_NOTHING;
        }

      time = g_get_real_time ();
      if (pointer_1_event_type == CLUTTER_NOTHING)
        {
          pointer_enter_time = time;
          pointer_1_event_type = CLUTTER_ENTER;
        }
      else if (pointer_1_event_type == CLUTTER_MOTION ||
               (pointer_1_event_type == CLUTTER_ENTER &&
                time - pointer_enter_time > GESTURE_TIMEOUT * 1000))
        {
          pointer_1_event_type = CLUTTER_MOTION;
          pointer_2_event_type = CLUTTER_NOTHING;
          pointer_1 = single_point;
          set_mouse_pointer (single_point->x, single_point->y);
        }
    }
  else if (left_point && right_point)
    {
      time = g_get_real_time ();
      if (pointer_1_event_type == CLUTTER_MOTION)
        {
          /* One hand entered when the other was already
             doing something */
          Point *point = NULL;

          if (pointer_2_event_type == CLUTTER_NOTHING)
            {
              pointer_enter_time = time;
              pointer_2_event_type = CLUTTER_ENTER;
            }
          else if (pointer_2_event_type == CLUTTER_ENTER &&
                   time - pointer_enter_time > GESTURE_TIMEOUT * 1000)
            {
              pointer_2_event_type = CLUTTER_BUTTON_PRESS;
              mouse_down (1);
            }

          if (pointer_1 == last_left_point)
            {
              point = left_point;
            }
          else if (pointer_1 == last_right_point)
            {
              point = right_point;
            }

          pointer_1 = point;
          if (point != NULL)
            {
              set_mouse_pointer (point->x, point->y);
            }
        }
      else
        {
          /* Both hands entered at the same time*/
          pointer_1 = left_point;
          pointer_2 = right_point;
          pointer_1_event_type = CLUTTER_SCROLL;
          pointer_2_event_type = CLUTTER_SCROLL;

          /* Skip the first time where both hands entered */
          if (last_left_point != NULL && last_right_point != NULL)
            {
              if (DOUBLE_HAND_WHEEL_MODE)
                {
                  interpret_wheel_gesture (pointer_1,
                                           pointer_2);
                }
              else
                {
                  interpret_pinch_gesture (pointer_1,
                                           pointer_2);
                }
            }
        }
    }
  else if (last_right_point || last_left_point)
    {
      both_hands_left ();
    }

  if (last_left_point != NULL)
    {
      g_slice_free (Point, last_left_point);
    }
  last_left_point = left_point;
  if (last_right_point != NULL)
    {
      g_slice_free (Point, last_right_point);
    }
  last_right_point = right_point;
}

static void
on_track_joints (GObject      *obj,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  BufferInfo *buffer_info;
  guint16 *reduced, *original;
  gint width, height;
  GError *error = NULL;

  buffer_info = (BufferInfo *) user_data;
  reduced = (guint16 *) buffer_info->reduced_buffer;
  original = (guint16 *) buffer_info->original_buffer;
  width = buffer_info->width;
  height = buffer_info->height;

  list = skeltrack_skeleton_track_joints_finish (skeleton,
                                                 res,
                                                 &error);

  if (error == NULL)
    {
      interpret_guestures (list, original, width, height);

      if (SHOW_SKELETON)
        clutter_cairo_texture_invalidate (CLUTTER_CAIRO_TEXTURE (depth_tex));
    }
  else
    {
      g_error_free (error);
    }

  g_slice_free1 (width * height * sizeof (guint16), reduced);
  g_slice_free (BufferInfo, buffer_info);
}

static void
grayscale_buffer_set_value (guchar *buffer, gint index, guchar value)
{
  buffer[index * 3] = value;
  buffer[index * 3 + 1] = value;
  buffer[index * 3 + 2] = value;
}

static BufferInfo *
process_buffer (guint16 *buffer,
                guint width,
                guint height,
                guint dimension_factor,
                guint threshold_begin,
                guint threshold_end)
{
  BufferInfo *buffer_info;
  gint i, j, reduced_width, reduced_height;
  guint16 *reduced_buffer;

  g_return_val_if_fail (buffer != NULL, NULL);

  reduced_width = (width - width % dimension_factor) / dimension_factor;
  reduced_height = (height - height % dimension_factor) / dimension_factor;

  reduced_buffer = g_slice_alloc0 (reduced_width * reduced_height *
                                   sizeof (guint16));

  for (i = 0; i < reduced_width; i++)
    {
      for (j = 0; j < reduced_height; j++)
        {
          gint index;
          guint16 value = 0;

          index = j * width * dimension_factor + i * dimension_factor;
          value = buffer[index];

          if (value < threshold_begin || value > threshold_end)
            {
              reduced_buffer[j * reduced_width + i] = 0;
              continue;
            }

          reduced_buffer[j * reduced_width + i] = value;
        }
    }

  buffer_info = g_slice_new0 (BufferInfo);
  buffer_info->reduced_buffer = reduced_buffer;
  buffer_info->original_buffer = buffer;
  buffer_info->reduced_width = reduced_width;
  buffer_info->reduced_height = reduced_height;
  buffer_info->width = width;
  buffer_info->height = height;

  return buffer_info;
}

static guchar *
create_grayscale_buffer (BufferInfo *buffer_info, gint dimension_reduction)
{
  gint i, j;
  gint size;
  guchar *grayscale_buffer;
  guint16 *reduced_buffer;

  reduced_buffer = buffer_info->reduced_buffer;

  size = buffer_info->width * buffer_info->height * sizeof (guchar) * 3;
  grayscale_buffer = g_slice_alloc (size);
  /* Paint it white */
  memset (grayscale_buffer, 255, size);

  for (i = 0; i < buffer_info->reduced_width; i++)
    {
      for (j = 0; j < buffer_info->reduced_height; j++)
        {
          if (reduced_buffer[j * buffer_info->reduced_width + i] != 0)
            {
              gint index = j * dimension_reduction * buffer_info->width +
                i * dimension_reduction;
              grayscale_buffer_set_value (grayscale_buffer, index, 0);
            }
        }
    }

  return grayscale_buffer;
}

static Point *
smooth_point (guint16 *buffer, guint width, guint height, SkeltrackJoint *joint)
{
  if (joint == NULL)
    return NULL;

  Point *closest;
  gint i, j, x, y, radius, min, count;
  radius = 16;
  x = joint->screen_x;
  y = joint->screen_y;

  if (x >= width || y >= height)
    return NULL;

  closest = g_slice_new0 (Point);

  closest->x = x;
  closest->y = y;
  closest->z = joint->z;
  min = closest->z - 50;

  count = 1;

  for (i = x - radius; i < x + radius; i+=2)
    {
      if (i < 0 || i >= width)
        continue;
      for (j = y - radius; j < y + radius; j+=2)
        {
          guint16 current;
          if (j < 0 || j >= height || (j == y && i == x))
            continue;

          current = buffer[j * width + i];
          if (current < closest->z && current >= min)
            {
              closest->x += x;
              closest->y += y;
              count++;
            }
        }
    }

  closest->x /= count;
  closest->y /= count;

  return closest;
}

static void
on_depth_frame (GFreenectDevice *kinect, gpointer user_data)
{
  gint width, height;
  gint dimension_factor;
  guchar *grayscale_buffer;
  guint16 *depth;
  BufferInfo *buffer_info;
  gsize len;
  GError *error = NULL;
  GFreenectFrameMode frame_mode;

  depth = (guint16 *) gfreenect_device_get_depth_frame_raw (kinect,
                                                            &len,
                                                            &frame_mode);

  width = frame_mode.width;
  height = frame_mode.height;

  g_object_get (skeleton, "dimension-reduction", &dimension_factor, NULL);

  buffer_info = process_buffer (depth,
                                width,
                                height,
                                dimension_factor,
                                THRESHOLD_BEGIN,
                                THRESHOLD_END);

  skeltrack_skeleton_track_joints (skeleton,
                                   buffer_info->reduced_buffer,
                                   buffer_info->reduced_width,
                                   buffer_info->reduced_height,
                                   NULL,
                                   on_track_joints,
                                   buffer_info);


  if (!SHOW_SKELETON)
    {
      grayscale_buffer = create_grayscale_buffer (buffer_info,
                                                  dimension_factor);
      if (! clutter_texture_set_from_rgb_data (CLUTTER_TEXTURE (depth_tex),
                                               grayscale_buffer,
                                               FALSE,
                                               width, height,
                                               0,
                                               3,
                                               CLUTTER_TEXTURE_NONE,
                                               &error))
        {
          g_error_free (error);
        }
      g_slice_free1 (width * height * sizeof (guchar) * 3,
                     grayscale_buffer);
    }
}

static void
paint_joint (cairo_t *cairo,
             SkeltrackJoint *joint,
             gint radius,
             const gchar *color_str)
{
  ClutterColor *color;

  if (joint == NULL)
    return;

  color = clutter_color_new (0, 0, 0, 200);
  clutter_color_from_string (color, color_str);

  cairo_set_line_width (cairo, 10);
  clutter_cairo_set_source_color (cairo, color);
  cairo_arc (cairo,
             joint->screen_x,
             joint->screen_y,
             radius * (THRESHOLD_END - THRESHOLD_BEGIN) / joint->z,
             0,
             G_PI * 2);
  cairo_fill (cairo);
  clutter_color_free (color);
}

static void
on_texture_draw (ClutterCairoTexture *texture,
                 cairo_t *cairo,
                 gpointer user_data)
{
  guint width, height;
  ClutterColor *color;
  SkeltrackJoint *head, *left_hand, *right_hand;
  if (list == NULL)
    return;

  head = skeltrack_joint_list_get_joint (list,
                                         SKELTRACK_JOINT_ID_HEAD);
  left_hand = skeltrack_joint_list_get_joint (list,
                                              SKELTRACK_JOINT_ID_LEFT_HAND);
  right_hand = skeltrack_joint_list_get_joint (list,
                                               SKELTRACK_JOINT_ID_RIGHT_HAND);

  /* Paint it white */
  clutter_cairo_texture_clear (texture);
  clutter_cairo_texture_get_surface_size (texture, &width, &height);
  color = clutter_color_new (255, 255, 255, 255);
  clutter_cairo_set_source_color (cairo, color);
  cairo_rectangle (cairo, 0, 0, width, height);
  cairo_fill (cairo);
  clutter_color_free (color);

  paint_joint (cairo, head, 50, "#FFF800");
  paint_joint (cairo, left_hand, 30, "#C2FF00");
  paint_joint (cairo, right_hand, 30, "#00FAFF");

  skeltrack_joint_list_free (list);
  list = NULL;
}

static void
set_info_text (void)
{
  gchar *title;
  title = g_strdup_printf ("<b>Current View:</b> %s\n"
                           "<b>Double hand mode:</b> %s\n"
                           "<b>Threshold:</b> %d",
                           SHOW_SKELETON ? "Skeleton" : "Point Cloud",
                           DOUBLE_HAND_WHEEL_MODE ? "Steering Wheel": "Pinch",
                           THRESHOLD_END);
  clutter_text_set_markup (CLUTTER_TEXT (info_text), title);
  g_free (title);
}

static void
set_threshold (gint difference)
{
  gint new_threshold = THRESHOLD_END + difference;
  if (new_threshold >= THRESHOLD_BEGIN + 300 &&
      new_threshold <= 4000)
    THRESHOLD_END = new_threshold;
}

static void
set_tilt_angle (GFreenectDevice *kinect, gdouble difference)
{
  GError *error = NULL;
  gdouble angle;
  angle = gfreenect_device_get_tilt_angle_sync (kinect, NULL, &error);
  if (error != NULL)
    {
      g_error_free (error);
      return;
    }

  if (angle >= -31 && angle <= 31)
    gfreenect_device_set_tilt_angle (kinect,
                                     angle + difference,
                                     NULL,
                                     NULL,
                                     NULL);
}

static gboolean
on_key_release (ClutterActor *actor,
                ClutterEvent *event,
                gpointer data)
{
  GFreenectDevice *kinect;
  guint key;
  g_return_val_if_fail (event != NULL, FALSE);

  kinect = GFREENECT_DEVICE (data);

  key = clutter_event_get_key_symbol (event);
  switch (key)
    {
    case CLUTTER_KEY_space:
      SHOW_SKELETON = !SHOW_SKELETON;
      break;
    case CLUTTER_KEY_Tab:
      DOUBLE_HAND_WHEEL_MODE = !DOUBLE_HAND_WHEEL_MODE;
      break;
    case CLUTTER_KEY_plus:
      set_threshold (100);
      break;
    case CLUTTER_KEY_minus:
      set_threshold (-100);
      break;
    case CLUTTER_KEY_Up:
      set_tilt_angle (kinect, 5);
      break;
    case CLUTTER_KEY_Down:
      set_tilt_angle (kinect, -5);
      break;
    }
  set_info_text ();
  return TRUE;
}

static ClutterActor *
create_instructions (void)
{
  ClutterActor *text;

  text = clutter_text_new ();
  clutter_text_set_markup (CLUTTER_TEXT (text),
                           "<b>Instructions:</b>\n"
                           "\tChange between double hand mode:  \tTab\n"
                           "\tChange between skeleton\n"
                           "\t  tracking and threshold view:  \tSpace bar\n"
                           "\tSet tilt angle:  \t\t\t\tUp/Down Arrows\n"
                           "\tIncrease threshold:  \t\t\t+/-");
  return text;
}

static void
on_destroy (ClutterActor *actor, gpointer data)
{
  GFreenectDevice *device = GFREENECT_DEVICE (data);
  gfreenect_device_stop_depth_stream (device, NULL);
  clutter_main_quit ();
}

static void
on_new_kinect_device (GObject      *obj,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  ClutterActor *stage, *instructions;
  GError *error = NULL;
  gint width = 640;
  gint height = 480;

  kinect = gfreenect_device_new_finish (res, &error);
  if (kinect == NULL)
    {
      g_debug ("Failed to created kinect device: %s", error->message);
      g_error_free (error);
      clutter_main_quit ();
      return;
    }

  g_debug ("Kinect device created!");

  g_debug ("SCREEN: %d %d", screen_width, screen_height);

  stage = clutter_stage_get_default ();
  clutter_stage_set_title (CLUTTER_STAGE (stage), "Skeltrack Desktop Control");
  clutter_actor_set_size (stage, width, height + 220);
  clutter_stage_set_user_resizable (CLUTTER_STAGE (stage), TRUE);

  g_signal_connect (stage, "destroy", G_CALLBACK (on_destroy), kinect);
  g_signal_connect (stage,
                    "key-release-event",
                    G_CALLBACK (on_key_release),
                    kinect);

  depth_tex = clutter_cairo_texture_new (width, height);
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), depth_tex);

  info_text = clutter_text_new ();
  set_info_text ();
  clutter_actor_set_position (info_text, 50, height + 20);
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), info_text);

  instructions = create_instructions ();
  clutter_actor_set_position (instructions, 50, height + 90);
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), instructions);

  clutter_actor_show_all (stage);

  skeleton = SKELTRACK_SKELETON (skeltrack_skeleton_new ());

  g_signal_connect (kinect,
                    "depth-frame",
                    G_CALLBACK (on_depth_frame),
                    NULL);

  g_signal_connect (depth_tex,
                    "draw",
                    G_CALLBACK (on_texture_draw),
                    NULL);

  gfreenect_device_set_tilt_angle (kinect, 0, NULL, NULL, NULL);

  gfreenect_device_start_depth_stream (kinect,
                                       GFREENECT_DEPTH_FORMAT_MM,
                                       NULL);
}

static void
quit (gint signale)
{
  signal (SIGINT, 0);

  clutter_main_quit ();
}

int
main (int argc, char *argv[])
{
  Screen *screen;

  display = XOpenDisplay (0);
  screen = XDefaultScreenOfDisplay (display);
  screen_width = XWidthOfScreen (screen);
  screen_height = XHeightOfScreen (screen);

  if (clutter_init (&argc, &argv) != CLUTTER_INIT_SUCCESS ||
      display == NULL || screen == NULL)
    {
      XCloseDisplay (display);
      return -1;
    }

  gfreenect_device_new (0,
                        GFREENECT_SUBDEVICE_CAMERA,
                        NULL,
                        on_new_kinect_device,
                        NULL);

  signal (SIGINT, quit);

  clutter_main ();

  if (last_left_point != NULL)
    g_slice_free (Point, last_left_point);
  if (last_right_point != NULL)
    g_slice_free (Point, last_right_point);

  if (kinect != NULL)
    g_object_unref (kinect);

  if (skeleton != NULL)
    {
      g_object_unref (skeleton);
    }

  XCloseDisplay (display);

  return 0;
}

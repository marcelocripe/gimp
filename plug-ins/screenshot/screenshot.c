/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * Screenshot plug-in
 * Copyright 1998-2007 Sven Neumann <sven@gimp.org>
 * Copyright 2003      Henrik Brix Andersen <brix@gimp.org>
 * Copyright 2012      Simone Karin Lehmann - OS X patches
 * Copyright 2016      Michael Natterer <mitch@gimp.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "screenshot.h"
#include "screenshot-freedesktop.h"
#include "screenshot-icon.h"
#include "screenshot-osx.h"
#include "screenshot-x11.h"
#include "screenshot-win32.h"

#include "libgimp/stdplugins-intl.h"


/* Defines */

#define PLUG_IN_PROC   "plug-in-screenshot"
#define PLUG_IN_BINARY "screenshot"
#define PLUG_IN_ROLE   "gimp-screenshot"

typedef struct _Screenshot      Screenshot;
typedef struct _ScreenshotClass ScreenshotClass;

struct _Screenshot
{
  GimpPlugIn      parent_instance;
};

struct _ScreenshotClass
{
  GimpPlugInClass parent_class;
};

#define SCREENSHOT_TYPE  (screenshot_get_type ())
#define SCREENSHOT (obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SCREENSHOT_TYPE, Screenshot))

GType                   screenshot_get_type         (void) G_GNUC_CONST;

static GList          * screenshot_query_procedures (GimpPlugIn           *plug_in);
static GimpProcedure  * screenshot_create_procedure (GimpPlugIn           *plug_in,
                                                     const gchar          *name);

static GimpValueArray * screenshot_run              (GimpProcedure        *procedure,
                                                     const GimpValueArray *args,
                                                     gpointer              run_data);

static GimpPDBStatusType   shoot               (GdkMonitor       *monitor,
                                                GimpImage       **image,
                                                GError          **error);

static gboolean            shoot_dialog        (GdkMonitor      **monitor);
static gboolean            shoot_quit_timeout  (gpointer          data);
static gboolean            shoot_delay_timeout (gpointer          data);


G_DEFINE_TYPE (Screenshot, screenshot, GIMP_TYPE_PLUG_IN)

GIMP_MAIN (SCREENSHOT_TYPE)
DEFINE_STD_SET_I18N


static ScreenshotBackend       backend           = SCREENSHOT_BACKEND_NONE;
static ScreenshotCapabilities  capabilities      = 0;
static GtkWidget              *select_delay_grid = NULL;
static GtkWidget              *shot_delay_grid   = NULL;

static ScreenshotValues shootvals =
{
  SHOOT_WINDOW, /* root window            */
  TRUE,         /* include WM decorations */
  0,            /* window ID              */
  0,            /* select delay           */
  0,            /* screenshot delay       */
  0,            /* coords of region dragged out by pointer */
  0,
  0,
  0,
  FALSE,        /* show cursor */
  SCREENSHOT_PROFILE_POLICY_MONITOR
};


static void
screenshot_class_init (ScreenshotClass *klass)
{
  GimpPlugInClass *plug_in_class = GIMP_PLUG_IN_CLASS (klass);

  plug_in_class->query_procedures = screenshot_query_procedures;
  plug_in_class->create_procedure = screenshot_create_procedure;
  plug_in_class->set_i18n         = STD_SET_I18N;
}

static void
screenshot_init (Screenshot *screenshot)
{
}

static GList *
screenshot_query_procedures (GimpPlugIn *plug_in)
{
  return g_list_append (NULL, g_strdup (PLUG_IN_PROC));
}

static GimpProcedure *
screenshot_create_procedure (GimpPlugIn  *plug_in,
                       const gchar *name)
{
  GimpProcedure *procedure = NULL;

  if (! strcmp (name, PLUG_IN_PROC))
    {
      procedure = gimp_procedure_new (plug_in, name,
                                      GIMP_PDB_PROC_TYPE_PLUGIN,
                                      screenshot_run, NULL, NULL);

      gimp_procedure_set_menu_label (procedure, N_("_Screenshot..."));
      gimp_procedure_add_menu_path (procedure, "<Image>/File/Create/Acquire");

      gimp_procedure_set_documentation
        (procedure,
         N_("Create an image from an area of the screen"),
         "The plug-in takes screenshots of an "
         "interactively selected window or of the desktop, "
         "either the whole desktop or an interactively "
         "selected region. When called non-interactively, it "
         "may grab the root window or use the window-id "
         "passed as a parameter.  The last four parameters "
         "are optional and can be used to specify the corners "
         "of the region to be grabbed."
         "On Mac OS X, "
         "when called non-interactively, the plug-in"
         "only can take screenshots of the entire root window."
         "Grabbing a window or a region is not supported"
         "non-interactively. To grab a region or a particular"
         "window, you need to use the interactive mode.",
         name);

      gimp_procedure_set_attribution (procedure,
                                      "Sven Neumann <sven@gimp.org>, "
                                      "Henrik Brix Andersen <brix@gimp.org>,"
                                      "Simone Karin Lehmann",
                                      "1998 - 2008",
                                      "v1.1 (2008/04)");


      gimp_procedure_set_icon_pixbuf (procedure,
                                      gdk_pixbuf_new_from_inline (-1, screenshot_icon,
                                                                  FALSE, NULL));

      GIMP_PROC_ARG_ENUM (procedure, "run-mode",
                          "Run mode",
                          "The run mode",
                          GIMP_TYPE_RUN_MODE,
                          GIMP_RUN_NONINTERACTIVE,
                          G_PARAM_READWRITE);

      GIMP_PROC_ARG_INT (procedure, "shoot-type",
                         "Shoot type",
                         "The shoot type { SHOOT-WINDOW (0), SHOOT-ROOT (1), "
                         "SHOOT-REGION (2) }",
                         0, 2, 0,
                         G_PARAM_READWRITE);

      GIMP_PROC_ARG_INT (procedure, "x1",
                         "X1",
                         "Region left x coord for SHOOT-WINDOW",
                         G_MININT, G_MAXINT, 0,
                         G_PARAM_READWRITE);

      GIMP_PROC_ARG_INT (procedure, "y1",
                         "Y1",
                         "Region top y coord for SHOOT-WINDOW",
                         G_MININT, G_MAXINT, 0,
                         G_PARAM_READWRITE);

      GIMP_PROC_ARG_INT (procedure, "x2",
                         "X2",
                         "Region right x coord for SHOOT-WINDOW",
                         G_MININT, G_MAXINT, 0,
                         G_PARAM_READWRITE);

      GIMP_PROC_ARG_INT (procedure, "y2",
                         "Y2",
                         "Region bottom y coord for SHOOT-WINDOW",
                         G_MININT, G_MAXINT, 0,
                         G_PARAM_READWRITE);

      GIMP_PROC_VAL_IMAGE (procedure, "image",
                           "Image",
                           "Output image",
                           FALSE,
                           G_PARAM_READWRITE);
    }

  return procedure;
}

static GimpValueArray *
screenshot_run (GimpProcedure        *procedure,
                const GimpValueArray *args,
                gpointer              run_data)
{
  GimpValueArray    *return_vals;
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;
  GimpRunMode        run_mode;
  GdkMonitor        *monitor = NULL;
  GimpImage         *image   = NULL;
  GError            *error   = NULL;

  gegl_init (NULL, NULL);

  run_mode = GIMP_VALUES_GET_ENUM (args, 0);
  gimp_ui_init (PLUG_IN_BINARY);

#ifdef PLATFORM_OSX
  if (! backend && screenshot_osx_available ())
    {
      backend      = SCREENSHOT_BACKEND_OSX;
      capabilities = screenshot_osx_get_capabilities ();

      /* on OS X, this just means shoot the shadow, default to nope */
      shootvals.decorate = FALSE;
    }
#endif

#ifdef G_OS_WIN32
  if (! backend && screenshot_win32_available ())
    {
      backend      = SCREENSHOT_BACKEND_WIN32;
      capabilities = screenshot_win32_get_capabilities ();
    }
#endif

#ifdef GDK_WINDOWING_X11
  if (! backend && screenshot_x11_available ())
    {
      backend      = SCREENSHOT_BACKEND_X11;
      capabilities = screenshot_x11_get_capabilities ();
    }
#endif
  if (! backend && screenshot_freedesktop_available ())
    {
      backend      = SCREENSHOT_BACKEND_FREEDESKTOP;
      capabilities = screenshot_freedesktop_get_capabilities ();
    }

  /* how are we running today? */
  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      /* Possibly retrieve data from a previous run */
      gimp_get_data (PLUG_IN_PROC, &shootvals);
      shootvals.window_id = 0;

      if ((shootvals.shoot_type == SHOOT_WINDOW &&
           ! (capabilities & SCREENSHOT_CAN_SHOOT_WINDOW)) ||
          (shootvals.shoot_type == SHOOT_REGION &&
           ! (capabilities & SCREENSHOT_CAN_SHOOT_REGION)))
        {
          /* Shoot root is the only type of shoot which is definitely
           * shared by all screenshot backends (basically just snap the
           * whole display setup).
           */
          shootvals.shoot_type = SHOOT_ROOT;
        }

      /* Get information from the dialog. Freedesktop portal comes with
       * its own dialog.
       */
      if (backend != SCREENSHOT_BACKEND_FREEDESKTOP)
        {
          if (! shoot_dialog (&monitor))
            status = GIMP_PDB_CANCEL;
        }
      else
        {
          /* This is ugly but in reality we have no idea on which monitor
           * a screenshot was taken from with portals. It's like a
           * better-than-nothing trick for easy single-display cases.
           */
          monitor = gdk_display_get_monitor (gdk_display_get_default (), 0);
        }
      break;

    case GIMP_RUN_NONINTERACTIVE:
      shootvals.shoot_type   = GIMP_VALUES_GET_INT (args, 1);
      shootvals.window_id    = GIMP_VALUES_GET_INT (args, 2);
      shootvals.select_delay = 0;
      shootvals.x1           = GIMP_VALUES_GET_INT (args, 3);
      shootvals.y1           = GIMP_VALUES_GET_INT (args, 4);
      shootvals.x2           = GIMP_VALUES_GET_INT (args, 5);
      shootvals.y2           = GIMP_VALUES_GET_INT (args, 6);

      if (! gdk_init_check (NULL, NULL))
        status = GIMP_PDB_CALLING_ERROR;

      if (! (capabilities & SCREENSHOT_CAN_PICK_NONINTERACTIVELY))
        {
          if (shootvals.shoot_type == SHOOT_WINDOW ||
              shootvals.shoot_type == SHOOT_REGION)
            {
              status = GIMP_PDB_CALLING_ERROR;
            }
        }
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      /* Possibly retrieve data from a previous run */
      gimp_get_data (PLUG_IN_PROC, &shootvals);
      break;

    default:
      break;
    }

  if (status == GIMP_PDB_SUCCESS)
    {
      status = shoot (monitor, &image, &error);
    }

  if (status == GIMP_PDB_SUCCESS)
    {
      gchar *comment = gimp_get_default_comment ();

      gimp_image_undo_disable (image);

      if (shootvals.profile_policy == SCREENSHOT_PROFILE_POLICY_SRGB)
        {
          GimpColorProfile *srgb_profile = gimp_color_profile_new_rgb_srgb ();

          gimp_image_convert_color_profile (image,
                                            srgb_profile,
                                            GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                            TRUE);
          g_object_unref (srgb_profile);
        }

      if (comment)
        {
          GimpParasite *parasite;

          parasite = gimp_parasite_new ("gimp-comment",
                                        GIMP_PARASITE_PERSISTENT,
                                        strlen (comment) + 1, comment);

          gimp_image_attach_parasite (image, parasite);
          gimp_parasite_free (parasite);

          g_free (comment);
        }

      gimp_image_undo_enable (image);

      if (run_mode == GIMP_RUN_INTERACTIVE)
        {
          /* Store variable states for next run */
          gimp_set_data (PLUG_IN_PROC, &shootvals, sizeof (ScreenshotValues));

          gimp_display_new (image);

          /* Give some sort of feedback that the shot is done */
          if (shootvals.select_delay > 0)
            {
              gdk_display_beep (gdk_monitor_get_display (monitor));
              /* flush so the beep makes it to the server */
              gdk_display_flush (gdk_monitor_get_display (monitor));
            }
        }
    }

  return_vals = gimp_procedure_new_return_values (procedure, status, error);

  if (status == GIMP_PDB_SUCCESS)
    GIMP_VALUES_SET_IMAGE (return_vals, 1, image);

  return return_vals;
}


/* The main Screenshot function */

static GimpPDBStatusType
shoot (GdkMonitor  *monitor,
       GimpImage  **image,
       GError     **error)
{
#ifdef PLATFORM_OSX
  if (backend == SCREENSHOT_BACKEND_OSX)
    return screenshot_osx_shoot (&shootvals, monitor, image, error);
#endif

#ifdef G_OS_WIN32
  if (backend == SCREENSHOT_BACKEND_WIN32)
    return screenshot_win32_shoot (&shootvals, monitor, image, error);
#endif

  if (backend == SCREENSHOT_BACKEND_FREEDESKTOP)
    return screenshot_freedesktop_shoot (&shootvals, monitor, image, error);

#ifdef GDK_WINDOWING_X11
  if (backend == SCREENSHOT_BACKEND_X11)
    return screenshot_x11_shoot (&shootvals, monitor, image, error);
#endif

  return GIMP_PDB_CALLING_ERROR; /* silence compiler */
}


/*  Screenshot dialog  */

static void
shoot_dialog_add_hint (GtkNotebook *notebook,
                       ShootType    type,
                       const gchar *hint)
{
  GtkWidget *label;

  label = g_object_new (GTK_TYPE_LABEL,
                        "label",   hint,
                        "wrap",    TRUE,
                        "justify", GTK_JUSTIFY_LEFT,
                        "xalign",  0.0,
                        "yalign",  0.0,
                        NULL);
  gimp_label_set_attributes (GTK_LABEL (label),
                             PANGO_ATTR_STYLE, PANGO_STYLE_ITALIC,
                             -1);

  gtk_notebook_insert_page (notebook, label, NULL, type);
  gtk_widget_show (label);
}

static void
shoot_radio_button_toggled (GtkWidget *widget,
                            GtkWidget *notebook)
{
  gimp_radio_button_update (widget, &shootvals.shoot_type);

  if (select_delay_grid)
    {
      if (shootvals.shoot_type == SHOOT_ROOT ||
          (shootvals.shoot_type == SHOOT_WINDOW &&
           ! (capabilities & SCREENSHOT_CAN_PICK_WINDOW)))
        {
          gtk_widget_hide (select_delay_grid);
        }
      else
        {
          gtk_widget_show (select_delay_grid);
        }
    }
  if (shot_delay_grid)
    {
      if (shootvals.shoot_type == SHOOT_WINDOW        &&
          (capabilities & SCREENSHOT_CAN_PICK_WINDOW) &&
          ! (capabilities & SCREENSHOT_CAN_DELAY_WINDOW_SHOT))
        {
          gtk_widget_hide (shot_delay_grid);
        }
      else
        {
          gtk_widget_show (shot_delay_grid);
        }
    }
  gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), shootvals.shoot_type);
}

static gboolean
shoot_dialog (GdkMonitor **monitor)
{
  GtkWidget     *dialog;
  GtkWidget     *main_vbox;
  GtkWidget     *notebook1;
  GtkWidget     *notebook2;
  GtkWidget     *frame;
  GtkWidget     *vbox;
  GtkWidget     *hbox;
  GtkWidget     *label;
  GtkWidget     *button;
  GtkWidget     *toggle;
  GtkWidget     *spinner;
  GtkWidget     *grid;
  GSList        *radio_group = NULL;
  GtkAdjustment *adj;
  gboolean       run;
  GtkWidget     *cursor_toggle = NULL;

  dialog = gimp_dialog_new (_("Screenshot"), PLUG_IN_ROLE,
                            NULL, 0,
                            gimp_standard_help_func, PLUG_IN_PROC,

                            _("_Cancel"), GTK_RESPONSE_CANCEL,
                            _("S_nap"),   GTK_RESPONSE_OK,

                            NULL);

  gimp_dialog_set_alternative_button_order (GTK_DIALOG (dialog),
                                           GTK_RESPONSE_OK,
                                           GTK_RESPONSE_CANCEL,
                                           -1);

  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);
  gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
                      main_vbox, FALSE, FALSE, 0);
  gtk_widget_show (main_vbox);


  /*  Create delay hints notebooks early  */
  notebook1 = g_object_new (GTK_TYPE_NOTEBOOK,
                            "show-border", FALSE,
                            "show-tabs",   FALSE,
                            NULL);
  notebook2 = g_object_new (GTK_TYPE_NOTEBOOK,
                            "show-border", FALSE,
                            "show-tabs",   FALSE,
                            NULL);

  /*  Area  */
  frame = gimp_frame_new (_("Area"));
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);

  /*  Single window  */
  if (capabilities & SCREENSHOT_CAN_SHOOT_WINDOW)
    {
      button = gtk_radio_button_new_with_mnemonic (radio_group,
                                                   _("Take a screenshot of "
                                                     "a single _window"));
      radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (button));
      gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
      gtk_widget_show (button);

      g_object_set_data (G_OBJECT (button), "gimp-item-data",
                         GINT_TO_POINTER (SHOOT_WINDOW));

      g_signal_connect (button, "toggled",
                        G_CALLBACK (shoot_radio_button_toggled),
                        notebook1);
      g_signal_connect (button, "toggled",
                        G_CALLBACK (shoot_radio_button_toggled),
                        notebook2);

      /*  Window decorations  */
      if (capabilities & SCREENSHOT_CAN_SHOOT_DECORATIONS)
        {
          hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
          gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
          gtk_widget_show (hbox);

          toggle = gtk_check_button_new_with_mnemonic (_("Include window _decoration"));
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle),
                                        shootvals.decorate);
          gtk_box_pack_start (GTK_BOX (hbox), toggle, TRUE, TRUE, 24);
          gtk_widget_show (toggle);

          g_signal_connect (toggle, "toggled",
                            G_CALLBACK (gimp_toggle_button_update),
                            &shootvals.decorate);

          g_object_bind_property (button, "active",
                                  toggle, "sensitive",
                                  G_BINDING_SYNC_CREATE);
        }
      /*  Mouse pointer  */
      if (capabilities & SCREENSHOT_CAN_SHOOT_POINTER)
        {
          hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
          gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
          gtk_widget_show (hbox);

          cursor_toggle = gtk_check_button_new_with_mnemonic (_("Include _mouse pointer"));
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cursor_toggle),
                                        shootvals.show_cursor);
          gtk_box_pack_start (GTK_BOX (hbox), cursor_toggle, TRUE, TRUE, 24);
          gtk_widget_show (cursor_toggle);

          g_signal_connect (cursor_toggle, "toggled",
                            G_CALLBACK (gimp_toggle_button_update),
                            &shootvals.show_cursor);

          g_object_bind_property (button, "active",
                                  cursor_toggle, "sensitive",
                                  G_BINDING_SYNC_CREATE);
        }


      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
                                    shootvals.shoot_type == SHOOT_WINDOW);
    }

  /*  Whole screen  */
  button = gtk_radio_button_new_with_mnemonic (radio_group,
                                               _("Take a screenshot of "
                                                 "the entire _screen"));
  radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (button));
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
  gtk_widget_show (button);

  g_object_set_data (G_OBJECT (button), "gimp-item-data",
                     GINT_TO_POINTER (SHOOT_ROOT));

  g_signal_connect (button, "toggled",
                    G_CALLBACK (shoot_radio_button_toggled),
                    notebook1);
  g_signal_connect (button, "toggled",
                    G_CALLBACK (shoot_radio_button_toggled),
                    notebook2);

  /*  Mouse pointer  */
  if (capabilities & SCREENSHOT_CAN_SHOOT_POINTER)
    {
      hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
      gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
      gtk_widget_show (hbox);

      toggle = gtk_check_button_new_with_mnemonic (_("Include _mouse pointer"));
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle),
                                    shootvals.show_cursor);
      gtk_box_pack_start (GTK_BOX (hbox), toggle, TRUE, TRUE, 24);
      gtk_widget_show (toggle);

      g_signal_connect (toggle, "toggled",
                        G_CALLBACK (gimp_toggle_button_update),
                        &shootvals.show_cursor);

      if (cursor_toggle)
        {
          g_object_bind_property (cursor_toggle, "active",
                                  toggle, "active",
                                  G_BINDING_BIDIRECTIONAL);
        }
      g_object_bind_property (button, "active",
                              toggle, "sensitive",
                              G_BINDING_SYNC_CREATE);
    }

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
                                shootvals.shoot_type == SHOOT_ROOT);

  /*  Dragged region  */
  if (capabilities & SCREENSHOT_CAN_SHOOT_REGION)
    {
      button = gtk_radio_button_new_with_mnemonic (radio_group,
                                                   _("Select a _region to grab"));
      radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (button));
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
                                    shootvals.shoot_type == SHOOT_REGION);
      gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);
      gtk_widget_show (button);

      g_object_set_data (G_OBJECT (button), "gimp-item-data",
                         GINT_TO_POINTER (SHOOT_REGION));

      g_signal_connect (button, "toggled",
                        G_CALLBACK (shoot_radio_button_toggled),
                        notebook1);
      g_signal_connect (button, "toggled",
                        G_CALLBACK (shoot_radio_button_toggled),
                        notebook2);
    }

  frame = gimp_frame_new (_("Delay"));
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, TRUE, TRUE, 0);
  gtk_widget_show (frame);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);

  /* Selection delay  */
  grid = gtk_grid_new ();
  select_delay_grid = grid;
  gtk_box_pack_start (GTK_BOX (vbox), grid, FALSE, FALSE, 0);
  /* Check if this delay must be hidden from start. */
  if (shootvals.shoot_type == SHOOT_REGION ||
      (shootvals.shoot_type == SHOOT_WINDOW &&
       capabilities & SCREENSHOT_CAN_PICK_WINDOW))
    {
      gtk_widget_show (select_delay_grid);
    }

  label = gtk_label_new (_("Selection delay: "));
  gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);
  gtk_widget_show (label);

  adj = gtk_adjustment_new (shootvals.select_delay,
                            0.0, 100.0, 1.0, 5.0, 0.0);
  spinner = gimp_spin_button_new (adj, 0, 0);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spinner), TRUE);
  gtk_grid_attach (GTK_GRID (grid), spinner, 1, 0, 1, 1);
  gtk_widget_show (spinner);

  g_signal_connect (adj, "value-changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &shootvals.select_delay);

  /*  translators: this is the unit label of a spinbutton  */
  label = gtk_label_new (_("seconds"));
  gtk_grid_attach (GTK_GRID (grid), label, 2, 0, 1, 1);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_widget_show (label);

  /*  Selection delay hints  */
  gtk_grid_attach (GTK_GRID (grid), notebook1, 0, 1, 3, 1);
  gtk_widget_show (notebook1);

  /* No selection delay for full-screen. */
  shoot_dialog_add_hint (GTK_NOTEBOOK (notebook1), SHOOT_ROOT, "");
  shoot_dialog_add_hint (GTK_NOTEBOOK (notebook1), SHOOT_REGION,
                         _("After the delay, drag your mouse to select "
                           "the region for the screenshot."));
#ifdef G_OS_WIN32
  shoot_dialog_add_hint (GTK_NOTEBOOK (notebook1), SHOOT_WINDOW,
                         _("Click in a window to snap it after delay."));
#else
  if (capabilities & SCREENSHOT_CAN_PICK_WINDOW)
    {
      shoot_dialog_add_hint (GTK_NOTEBOOK (notebook1), SHOOT_WINDOW,
                             _("At the end of the delay, click in a window "
                               "to snap it."));
    }
  else
    {
      shoot_dialog_add_hint (GTK_NOTEBOOK (notebook1), SHOOT_WINDOW, "");
    }
#endif
  gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook1), shootvals.shoot_type);

  /* Screenshot delay  */
  grid = gtk_grid_new ();
  shot_delay_grid = grid;
  gtk_box_pack_start (GTK_BOX (vbox), grid, FALSE, FALSE, 0);
  if (shootvals.shoot_type != SHOOT_WINDOW          ||
      ! (capabilities & SCREENSHOT_CAN_PICK_WINDOW) ||
      (capabilities & SCREENSHOT_CAN_DELAY_WINDOW_SHOT))
    {
      gtk_widget_show (grid);
    }

  label = gtk_label_new_with_mnemonic (_("Screenshot dela_y: "));
  gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);
  gtk_widget_show (label);

  adj = gtk_adjustment_new (shootvals.screenshot_delay,
                            0.0, 100.0, 1.0, 5.0, 0.0);
  spinner = gimp_spin_button_new (adj, 0, 0);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spinner), TRUE);
  gtk_grid_attach (GTK_GRID (grid), spinner, 1, 0, 1, 1);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (spinner));
  gtk_widget_show (spinner);

  g_signal_connect (adj, "value-changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &shootvals.screenshot_delay);

  /*  translators: this is the unit label of a spinbutton  */
  label = gtk_label_new (_("seconds"));
  gtk_grid_attach (GTK_GRID (grid), label, 2, 0, 1, 1);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_widget_show (label);

  /*  Screenshot delay hints  */
  gtk_grid_attach (GTK_GRID (grid), notebook2, 0, 1, 3, 1);
  gtk_widget_show (notebook2);

  shoot_dialog_add_hint (GTK_NOTEBOOK (notebook2), SHOOT_ROOT,
                         _("After the delay, the screenshot is taken."));
  shoot_dialog_add_hint (GTK_NOTEBOOK (notebook2), SHOOT_REGION,
                         _("Once the region is selected, it will be "
                           "captured after this delay."));
  if (capabilities & SCREENSHOT_CAN_PICK_WINDOW)
    {
      shoot_dialog_add_hint (GTK_NOTEBOOK (notebook2), SHOOT_WINDOW,
                             _("Once the window is selected, it will be "
                               "captured after this delay."));
    }
  else
    {
      shoot_dialog_add_hint (GTK_NOTEBOOK (notebook2), SHOOT_WINDOW,
                             _("After the delay, the active window "
                               "will be captured."));
    }
  gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook2), shootvals.shoot_type);

  /*  Color profile  */
  frame = gimp_int_radio_group_new (TRUE,
                                    _("Color Profile"),
                                    G_CALLBACK (gimp_radio_button_update),
                                    &shootvals.profile_policy, NULL,
                                    shootvals.profile_policy,

                                    _("Tag image with _monitor profile"),
                                    SCREENSHOT_PROFILE_POLICY_MONITOR,
                                    NULL,

                                    _("Convert image to sR_GB"),
                                    SCREENSHOT_PROFILE_POLICY_SRGB,
                                    NULL,

                                    NULL);
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);


  gtk_widget_show (dialog);

  run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

  if (run)
    {
      /* get the screen on which we are running */
      *monitor = gimp_widget_get_monitor (dialog);
    }

  gtk_widget_destroy (dialog);

  if (run)
    {
      /*  A short timeout to give the server a chance to
       *  redraw the area that was obscured by our dialog.
       */
      g_timeout_add (100, shoot_quit_timeout, NULL);
      gtk_main ();
    }

  return run;
}

static gboolean
shoot_quit_timeout (gpointer data)
{
  gtk_main_quit ();

  return FALSE;
}


static gboolean
shoot_delay_timeout (gpointer data)
{
  gint *seconds_left = data;

  (*seconds_left)--;

  if (!*seconds_left)
    gtk_main_quit ();

  return *seconds_left;
}


/*  public functions  */

void
screenshot_delay (gint seconds)
{
  g_timeout_add (1000, shoot_delay_timeout, &seconds);
  gtk_main ();
}

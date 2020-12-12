#include "config.h"
#include "libbuzztrax-gst/propertymeta.h"

#include "src/voice.h"
#include <unistd.h>
#include <stdio.h>

struct _GstBtAlphaJunoCtlV
{
  GstObject parent;
	
  guint channel;
  guint note_midi_last;
  guint velocity;
  guint modulation;
  gboolean hold;
  guint aftertouch;
  GstBtNote note;
};

enum
{
  PROP_VELOCITY = 1,
  PROP_MODULATION,
  PROP_HOLD,
  PROP_AFTERTOUCH,
  PROP_NOTE
};

G_DEFINE_TYPE (GstBtAlphaJunoCtlV, gstbt_alphajunoctlv, GST_TYPE_OBJECT)

static void gstbt_alphajunoctlv_init(GstBtAlphaJunoCtlV* const self) {
  self->note_midi_last = -1;
}

//-- property meta interface implementations

static inline void _note_off(GstBtAlphaJunoCtlV* const self, const int fd, const int note_midi) {
  char out[] = {0x80 + self->channel, note_midi, 0x00};
  _midi_write(fd, out, sizeof(out));
  self->note_midi_last = -1;
}

static void _set_property(GObject* const object, const guint prop_id, const GValue* const value,
						  GParamSpec* const pspec) {
  GstBtAlphaJunoCtlV *self = GSTBT_ALPHAJUNOCTLV(object);

  switch (prop_id)
  {
  case PROP_NOTE:
  {
	guint note = g_value_get_enum(value);

	// 'None' notes are regularly sent by buzztrax.
	if (note == GSTBT_NOTE_NONE)
	  break;
	
	const int fd = gstbt_alphajunoctl_midiout_get(gst_object_get_parent(&self->parent));
	if (note == GSTBT_NOTE_OFF) {
	  _note_off(self, fd, self->note_midi_last);
	} else {
	  if (self->note_midi_last != -1) {
		char out[] = {0x80 + self->channel, self->note_midi_last, 0x00};
		_midi_write(fd, out, sizeof(out));
	  }

	  guint note_midi = 12 + MIN((note % 16) + (12 * (note / 16)), 108);
	  char out[] = {0x90 + self->channel, note_midi, 0x7F};
	  _midi_write(fd, out, sizeof(out));
	  self->note_midi_last = note_midi;
	}
  }
  break;
	
  case PROP_VELOCITY:
	break;
  case PROP_MODULATION:
	break;
  case PROP_HOLD:
	break;
  case PROP_AFTERTOUCH:
	break;
  default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	break;
  }
}

static void _get_property (GObject* const object, const guint prop_id, GValue* const value,
						   GParamSpec* const pspec)
{
//  GstBtAlphaJunoCtlV *self = GSTBT_ALPHAJUNOCTLV (object);

  switch (prop_id)
  {
  case PROP_VELOCITY:
	break;
  case PROP_MODULATION:
	break;
  case PROP_HOLD:
	break;
  case PROP_AFTERTOUCH:
	break;
  default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	break;
  }
}

void gstbt_alphajunoctlv_process(GstBtAlphaJunoCtlV* const self, GstBuffer* const gstbuf) {
  // Necessary to update parameters from pattern.
  //
  // The parent machine is responsible for delgating process to any children it has; the pattern control group
  // won't have called it for each voice. Although maybe it should?
  gst_object_sync_values((GstObject*)self, GST_BUFFER_PTS(gstbuf));
}

void gstbt_alphajunoctlv_noteall_off(GstBtAlphaJunoCtlV* const self) {
  const int fd = gstbt_alphajunoctl_midiout_get(gst_object_get_parent(&self->parent));
  for (int i = 12; i <= 108; ++i) {
	_note_off(self, fd, i);
  }
}

static void gstbt_alphajunoctlv_class_init(GstBtAlphaJunoCtlVClass* const klass) {
  GObjectClass* const gobject_class = G_OBJECT_CLASS(klass);
  gobject_class->set_property = _set_property;
  gobject_class->get_property = _get_property;

  const GParamFlags flags = (GParamFlags)
	(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_property(
	gobject_class, PROP_VELOCITY,
	g_param_spec_uint ("velocity", "Velocity", "", 0, 0x7F, 0x7F, flags));

  g_object_class_install_property(
	gobject_class, PROP_MODULATION,
	g_param_spec_uint ("modulation", "Modulation", "", 0, 0x7F, 0, flags));

  g_object_class_install_property(
	gobject_class, PROP_HOLD,
	g_param_spec_boolean ("hold", "Hold", "", FALSE, flags));

  g_object_class_install_property(
	gobject_class, PROP_AFTERTOUCH,
	g_param_spec_uint ("aftertouch", "Aftertouch", "", 0, 0x7F, 0x7F, flags));

  g_object_class_install_property(
	gobject_class, PROP_NOTE,
	g_param_spec_enum ("note", "Musical note", "", GSTBT_TYPE_NOTE, GSTBT_NOTE_NONE,
					   G_PARAM_WRITABLE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
}

GstBtAlphaJunoCtlV* gstbt_alphajunoctlv_new(const int channel) {
  GstBtAlphaJunoCtlV* result = (GstBtAlphaJunoCtlV*)g_object_new(gstbt_alphajunoctlv_get_type(), NULL);
  result->channel = channel;
  return result;
}

/*
  Additive synth for Buzztrax
  Copyright (C) 2020 David Beswick

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

typedef enum {
  PROP_NONE = 0,
  PROP_FREQ_MAX,
  PROP_SUM_START_IDX,
  PROP_AMP_POW_BASE,
  PROP_AMP_EXP_IDX_MUL,
  PROP_AMPFREQ_SCALE_IDX_MUL,
  PROP_AMPFREQ_SCALE_OFFSET,
  PROP_AMPFREQ_SCALE_EXP,
  PROP_AMP_BOOST_CENTER,
  PROP_AMP_BOOST_SHARPNESS,
  PROP_AMP_BOOST_EXP,
  PROP_AMP_BOOST_DB,
  PROP_RINGMOD_RATE,
  PROP_RINGMOD_DEPTH,
  PROP_RINGMOD_OT_OFFSET,
  PROP_BEND,
  PROP_STEREO,
  PROP_VIRTUAL_VOICES,
  PROP_VOL,
  N_PROPERTIES_SRATE /*< skip >*/
} AdditivePropsSrate;

typedef enum {
  GSTBT_LFO_FLOAT_WAVEFORM_SINE,
  GSTBT_LFO_FLOAT_WAVEFORM_SQUARE,
  GSTBT_LFO_FLOAT_WAVEFORM_SAW,
  GSTBT_LFO_FLOAT_WAVEFORM_REVERSE_SAW,
  GSTBT_LFO_FLOAT_WAVEFORM_TRIANGLE
} GstBtLfoFloatWaveform;

typedef enum {
  GSTBT_ADSR_PROP_ATTACK_LEVEL = 0,
  GSTBT_ADSR_PROP_ATTACK_SECS,
  GSTBT_ADSR_PROP_ATTACK_POW,
  GSTBT_ADSR_PROP_SUSTAIN_LEVEL,
  GSTBT_ADSR_PROP_DECAY_SECS,
  GSTBT_ADSR_PROP_DECAY_POW,
  GSTBT_ADSR_PROP_RELEASE_SECS,
  GSTBT_ADSR_PROP_RELEASE_POW,
  GSTBT_ADSR_PROP_N /*< skip >*/
} GstBtAdsrProp;

typedef enum {
  GSTBT_LFO_FLOAT_PROP_NONE = -1,
  GSTBT_LFO_FLOAT_PROP_AMPLITUDE,
  GSTBT_LFO_FLOAT_PROP_FREQUENCY,
  GSTBT_LFO_FLOAT_PROP_SHAPE,
  GSTBT_LFO_FLOAT_PROP_FILTER,
  GSTBT_LFO_FLOAT_PROP_OFFSET,
  GSTBT_LFO_FLOAT_PROP_PHASE,
  GSTBT_LFO_FLOAT_PROP_WAVEFORM,
  GSTBT_LFO_FLOAT_PROP_N /*< skip >*/
} GstbtLfoFloatProp;

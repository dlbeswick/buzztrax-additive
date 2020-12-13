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
  PROP_FREQ_MAX = 1,
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
  PROP_VOL,
  N_PROPERTIES_SRATE
} PropsSrate;

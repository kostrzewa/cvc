/***********************************************************************
 *
 * Copyright (C) 2013 Bartosz Kostrzewa
 *
 * This file is part of CVC.
 *
 * CVC is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * CVC is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with CVC.  If not, see <http://www.gnu.org/licenses/>.
 *
 ************************************************************************/
 
/* See meson.hpp for a description of what this class and its static class members
* represent */
 
#ifndef NEUTRAL_CONN_MESON_20_HPP_
#define NEUTRAL_CONN_MESON_20_HPP_

#include "meson.hpp"

/* Gamma combinations for 20 neutral connected meson correlators from hl_conn.cc in the "Contractions"
 * code by Marc Wagner and Carsten Urbach.
 *  
 * pion:
 * g5-g5, g5-g0g5, g0g5-g5, g0g5-g0g5, 1-1, g5-1, 1-g5, g0g5-1, 1-g0g5
 * rho:
 * gig0-gig0, gi-gi, gig0g5-gig0g5, gig0-gi, gi-gig0, gig0-gig0g5, gig0g5-gig0, gi-gig0g5, gig0g5-gi
 * a0, b1:
 * g0-g0, gig5-gig5 
 * */

class neutral_conn_meson_20 : public meson 
{
  public: 
    neutral_conn_meson_20();
                          
  protected:
    static const string neutral_conn_meson_20_name;
    static const unsigned int neutral_conn_meson_20_N_correlators;
    static const unsigned int neutral_conn_meson_20_is_vector_correl[20];
    static const int neutral_conn_meson_20_isimag[20];
    static const int neutral_conn_meson_20_gindex1[40];
    static const int neutral_conn_meson_20_gindex2[40];
    static const double neutral_conn_meson_20_isneg[20];
    static const double neutral_conn_meson_20_vsign[30];
    static const double neutral_conn_meson_20_conf_gamma_sign[10];
};

#endif // NEUTRAL_CONN_MESON_20_HPP_

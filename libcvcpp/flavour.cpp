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

#include <string.h>

#include "flavour.hpp"
#include "global.h"
#include "Q_phi.h"

flavour::flavour() {
  initialized = false;
}

flavour::flavour( flavour_params i_params ) {
  initialized = false;
  init( i_params );
}

void flavour::init() {
  if( initialized ) {
    propagators.clear();
    initialized = false;
    init();
  } else {
    deb_printf(1,"# [flavour::init] Initialising flavour %s.\n",params.name.c_str());
    
    propagators.resize(params.no_masses);
    for( unsigned int mass = 0; mass < params.no_masses; ++mass ) {
      //deb_printf(1,"# [flavour::init] Initialising mass %f for flavour %s.\n", params.mass[mass], params.name);
      propagators[mass].resize(params.no_smearing_combinations);
      for( unsigned int smearing = 0; smearing < params.no_smearing_combinations; ++smearing ) {
        propagators[mass][smearing].resize(params.n_c*params.n_s);
        for( unsigned int index = 0; index < params.n_c*params.n_s; ++index ) {
          deb_printf(1,"# [flavour::init] Initialising propagator for flavour %s, smearing_combination %s, mass %e, index %u\n", 
            params.name.c_str(), smear_index_to_string(smearing).c_str(), params.masses[mass], index);
          
          unsigned int mass_index = params.first_mass_index+mass;
          
          unsigned int smearing_index = ( smearing < 2 ? 0 : 1 );
          
          /* propagator files can contain multiple propagators. either just two
           * when up and down are stored in the same file or 2*2*params.n_c*params.n_s
           * when all indices are stored in the same file 
           * (one factor of two comes from considering local and smeared sources
           * which are stupidly stored as consecutive indices)
           * in the case of MMS files, there are no explicit down propagators
           * scidac_offset keeps track of which propagator to read out */
           
          unsigned int scidac_offset = 0;
          if(params.splitted_propagator == false) {
            if(params.in_mms_file == true) {
              scidac_offset = smearing_index*params.n_c*params.n_s + index;
            } else {
              scidac_offset = 2*smearing_index*params.n_c*params.n_s + 2*index;
            }
          }
          
          /* up and down propagators are stored in the same file, unless
           * we are dealing with MMS files, in which case down propagators
           * are not computed explicitly */
          if(params.type == down_type && params.in_mms_file != true){
            scidac_offset += 1;
          }
          
          /* the complication of scidac_offset is reflected also in filename_index */
          unsigned int filename_index = smearing_index*params.n_c*params.n_s + index;
          
          string filename = construct_propagator_filename( mass_index, filename_index );
          
          propagators[mass][smearing][index].init( filename, smearing, scidac_offset, params.in_mms_file );
          
          /* propagator::read_from_x() applies Qf5 if we're dealing with an MMS propagator
           * this currently uses g_kappa and g_mu, so we have to set g_mu and g_kappa accordingly */
          double mu_save = g_mu;
          double kappa_save = g_kappa;
          if ( params.in_mms_file ) {
            g_mu = (params.type == down_type ? -1 : 1) * params.masses[mass];
            g_kappa = params.kappa;
          }
          
          /* for smearing indices 1 and 3 we can reuse data and just copy the previous propagator */
          if( smearing % 2 == 0 ) {
            propagators[mass][smearing][index].read_from_file();
          } else {
            propagators[mass][smearing][index].read_from_memory( propagators[mass][smearing-1][index] );
          }
          
          // reset g_kappa and g_mu
          if( params.in_mms_file ) {
            g_kappa = kappa_save;
            g_mu = mu_save;
          }
          
        }
      }
    } 
    initialized = true;
  }    
}

void flavour::init( flavour_params i_params ) {
  params = i_params;
  init();
}

/* these are the types of filenames that exist with the zero-padded width of the numbers given by (#)
 * 
 * splitted:
 * basename.confnum(4).t_source(2).ix(2).[h]inverted
 * basename.operator_id(2).confnum(4).t_source(2).ix(2).cgmms.mass(2).inverted
 * 
 * combined:
 * basename.confnum(4).t_source(2).[h]inverted
 * basename.operator_id(2).confnum(4).t_source(2).cgmms.mass(2).inverted
 * 
 * volume source:
 * basename.confnum(4).sample(5).inverted
 * basename.operator_id(2).confnum(4).t_source(2).mass(2).inverted
 * 
 * these are further specified by a subdirectory in which propagators may be stored
 * as well as a further subdirectory in case multiple masses are provided 
 * as non-MMS propagators
 * 
 */

string flavour::construct_propagator_filename( const unsigned int i_mass, const unsigned int i_index ) {
  stringstream filename;
  
  // TODO: add support for volume sources?
  //       add support for hinverted? 
  
  filename << setfill('0');
  
  // propagators stored in subdirectory
  if(params.propagator_dirname != string("") ) {
    filename << params.propagator_dirname << "/";
  }
  
  // multiple masses not in mms file but in subdirectories
  if(params.no_masses > 1 && params.in_mms_file == false) {
    filename << params.propagator_dirname << setw(2) << i_mass << "/";
  }
  filename << params.propagator_basename << ".";
  
  if(params.in_mms_file){
    // TODO: remove from tmLQCD, generalize in tmLQCD or generalize here
    filename << setw(2) << 0 << "."; // operator_id, usually 0
  } 
  
  filename << setw(4) << Nconf << ".";
  
  filename << setw(2) << params.source_timeslice << ".";
  
  if(params.splitted_propagator){
    filename << setw(2) << i_index << ".";
  }
  
  if(params.in_mms_file){
    filename << "cgmms." << setw(2) << i_mass << ".";
  }
  
  filename << "inverted";
  
  return filename.str();
}

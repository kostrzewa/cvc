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

/* Data structure which provides a simple interface to allocate and free
 * memory for the storage of correlators
 * The implementation is ugly though..
 */
 
#ifndef CORRELATOR_MEMORY_HPP_
#define CORRELATOR_MEMORY_HPP_

#include <vector>

// forward declarations
class correlator;

using namespace std;

class correlator_memory {
  public:
    correlator_memory();
    correlator_memory(const correlator_memory&);
    ~correlator_memory();
    
    // somewhat safely forego access limitations to the actual correls object by
    // at least making sure that it has the right size
    // the correlator storage has three levels (outer to inner):
    // flavour_combinations, gamma matrix combinations, smearing_combinations
    vector< vector< vector<correlator*> > >* get_correls_pointer(const unsigned int N_correlators, const unsigned int no_smearing_combinations, const unsigned no_flavour_combinations); 
    double* get_temp_correl_pointer();
    double* get_temp_vector_correl_pointer();
    
    void zero_out();
    void print_info();
    
  private:
    void init();
    void de_init();
  
    static bool initialized;
    static unsigned int ref_count;
    static vector< vector< vector<correlator*> > > correls;
    static double* temp_correl;
    static double* temp_vector_correl;
    static double* allreduce_buffer;
};

#endif // CORRELATOR_MEMORY_HPP_

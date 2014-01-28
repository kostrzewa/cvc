/***********************************************************************                                                             
 * Copyright (C) 2002,2003,2004,2005,2006,2007,2008 Carsten Urbach
 * Copyright (C) 2012 Bartosz Kostrzewa (gettime.[c,h])
 *
 * This file has been copied from tmLQCD.
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
 * along with tmLQCD.  If not, see <http://www.gnu.org/licenses/>.
***********************************************************************/

#ifndef _GETTIME_H
#define _GETTIME_H

/* gettime provides a time measurement with the BGL real time ticker,
   MPI_Wtime, clock_gettime and clock in decreasing order of preference
   depending on availability. Except for clock(), all these measurements
   are good representations of walltime */
   
/* in CVC, the clock_gettime method is currently not supported */

double gettime(void);

#endif /* _GETTIME_H */

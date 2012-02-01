/****************************************************
 * hl_conn_3.c
 *
 * Thu Apr  8 09:20:56 CET 2010
 *
 * PURPOSE:
 * TODO:
 * DONE:
 *
 ****************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef MPI
#  include <mpi.h>
#endif
#include <getopt.h>

#define MAIN_PROGRAM

#include "cvc_complex.h"
#include "cvc_linalg.h"
#include "global.h"
#include "cvc_geometry.h"
#include "cvc_utils.h"
#include "mpi_init.h"
#include "io.h"
#include "propagator_io.h"
#include "Q_phi.h"
#include "fuzz.h"
#include "fuzz2.h"
#include "read_input_parser.h"
#include "smearing_techniques.h"

void usage() {
  fprintf(stdout, "Code to perform contractions for connected contributions\n");
  fprintf(stdout, "Usage:    [options]\n");
  fprintf(stdout, "Options: -v verbose [no effect, lots of stdout output it]\n");
  fprintf(stdout, "         -f input filename [default cvc.input]\n");
  fprintf(stdout, "         -l Nlong for fuzzing [default -1, no fuzzing]\n");
  fprintf(stdout, "         -a no of steps for APE smearing [default -1, no smearing]\n");
  fprintf(stdout, "         -k alpha for APE smearing [default 0.]\n");
#ifdef MPI
  MPI_Abort(MPI_COMM_WORLD, 1);
  MPI_Finalize();
#endif
  exit(0);
}

/* #if defined _MMS_ALL_MASS_COMB */
#define _MMS_MASS_DIAGONAL
#undef _MMS_LIGHT_ALL_HEAVY
/* #endif */

int main(int argc, char **argv) {
  
  int c, i, j, k, ll, sl;
  int count, hidx;
  int filename_set = 0;
  int timeslice, mms1=0, mms2=0, mms2_min=1;
  int light_nomms=0;
  int x0, x1, x2, ix, idx;
  int VOL3;
  int n_c=1, n_s=4;
  int K=21, itype;
  int *xgindex1=NULL, *xgindex2=NULL, *xisimag=NULL;
  double *xvsign=NULL;
  double *cconn  = (double*)NULL;
#ifdef MPI
  double *buffer = (double*)NULL;
#endif
  double *work=NULL;
  int sigmalight=0, sigmaheavy=0;
  double *mms_masses=NULL, mulight=0., muheavy=0.;
  double contact_term;
  char *mms_extra_masses_file="cvc.extra_masses.input";
  
  int verbose = 0;
  char filename[200];
  double ratime, retime;
  double plaq;
  double *gauge_field_timeslice=NULL, *gauge_field_f=NULL;
  double **chi=NULL, **psi=NULL;
  double *Ctmp=NULL;
  FILE *ofs=NULL;
/*  double sign_adj5[] = {-1., -1., -1., -1., +1., +1., +1., +1., +1., +1., -1., -1., -1., 1., -1., -1.}; */
  double c_conf_gamma_sign[]  = {1., 1., 1., 1., 1., -1., -1., -1., -1.};
  double n_conf_gamma_sign[] = {1., 1., 1., 1., 1.,  1.,  1.,  1.,  1.};
  double *conf_gamma_sign=NULL;

  /**************************************************************************************************
   * charged stuff
   * here we loop over ll, ls, sl, ss (order source-sink)
   * pion:
   * g5-g5, g5-g0g5, g0g5-g5, g0g5-g0g5, g0-g0, g5-g0, g0-g5, g0g5-g0, g0-g0g5
   * rho:
   * gig0-gig0, gi-gi, gig5-gig5, gig0-gi, gi-gig0, gig0-gig5, gig5-gig0, gi-gig5, gig5-gi
   * a0, b1:
   * 1-1, gig0g5-gig0g5
   * conserved vector current
   * g5-g0_conserved
   **************************************************************************************************/
  int gindex1[] = {5, 5, 6, 6, 0, 5, 0, 6, 0,
                   10, 11, 12, 1, 2, 3, 7, 8, 9, 10, 11, 12, 1, 2, 3, 10, 11, 12, 7, 8, 9, 1, 2, 3, 7, 8, 9,
                   4, 13, 14, 15};

  int gindex2[] = {5, 6, 5, 6, 0, 0, 5, 0, 6,
                   10, 11, 12, 1, 2, 3, 7, 8, 9, 1, 2, 3, 10, 11, 12, 7, 8, 9, 10, 11, 12, 7, 8, 9, 1, 2, 3,
                   4, 13, 14, 15};

  /* due to twisting we have several correlators that are purely imaginary */
  int isimag[]  = {0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1};


  /**************************************************************************************************
   * neutral stuff 
   * here we loop over ll, ls, sl, ss (order source-sink)
   * pion:
   * g5-g5, g5-g0g5, g0g5-g5, g0g5-g0g5, 1-1, g5-1, 1-g5, g0g5-1, 1-g0g5
   * rho:
   * gig0-gig0, gi-gi, gig0g5-gig0g5, gig0-gi, gi-gig0, gig0-gig0g5, gig0g5-gig0, gi-gig0g5, gig0g5-gi
   * a0, b1:
   * g0-g0, gig5-gig5
   * conserved vector current
   * g5-g0_conserved
   **************************************************************************************************/
  int ngindex1[] = {5, 5, 6, 6, 4, 5, 4, 6, 4,
                    10, 11, 12, 1, 2, 3, 13, 14, 15, 10, 11, 12, 1, 2, 3, 10, 11, 12, 13, 14, 15, 1, 2, 3, 13, 14, 15,
                    0, 7, 8, 9};
  int ngindex2[] = {5, 6, 5, 6, 4, 4, 5, 4, 6,
                    10, 11, 12, 1, 2, 3, 13, 14, 15, 1, 2, 3, 10, 11, 12, 13, 14, 15, 10, 11, 12, 13, 14, 15, 1, 2, 3,
                    0, 7, 8, 9};
  int nisimag[]  = {0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1};


  double isneg_std[]=    {+1., -1., +1., -1., +1., +1., +1., +1., -1.,
                          -1., +1., -1., -1., +1., +1., +1., -1., +1.,
                          +1., -1., +1.};
  double isneg[21];

  /* every correlator for the rho part including gig0 either at source
   * or at sink has a different relative sign between the 3 contributions */
  double vsign[]= {1., 1., 1., 1., 1., 1., 1., 1., 1., 1., -1., 1., 1., -1., 1., 1., -1., 1.,
                   1., -1., 1., 1., 1., 1., 1., 1., 1., 1., 1., 1., 1.};
  double nvsign[] ={1., 1., 1., 1., 1., 1., 1., 1., 1., 1., -1., 1., 1., -1., 1., 1., -1., 1.,
                    1., 1., 1., 1., -1., 1., 1., -1., 1., 1., -1., 1., 1.};
 
#ifdef MPI
  MPI_Status status;
#endif

#ifdef MPI
  MPI_Init(&argc, &argv);
#endif

  while ((c = getopt(argc, argv, "h?vlf:p:m:")) != -1) {
    switch (c) {
    case 'v':
      verbose = 1;
      break;
    case 'f':
      strcpy(filename, optarg);
      filename_set=1;
      break;
    case 'p':
      n_c = atoi(optarg);
      break;
    case 'm':
      mms2_min = atoi(optarg);
      break;
    case 'l':
      light_nomms = 1;
      break;
    case 'h':
    case '?':
    default:
      usage();
      break;
    }
  }

  /* set the default values */
  if(filename_set==0) strcpy(filename, "cvc.input");
  fprintf(stdout, "# reading input from file %s\n", filename);
  read_input_parser(filename);

  /* some checks on the input data */
  if((T_global == 0) || (LX==0) || (LY==0) || (LZ==0)) {
    if(g_proc_id==0) fprintf(stdout, "T and L's must be set\n");
    usage();
  }

  /* initialize MPI parameters */
  mpi_init(argc, argv);

  VOL3 = LX*LY*LZ;
  fprintf(stdout, "# [%2d] parameters:\n"\
                  "# [%2d] T_global     = %3d\n"\
                  "# [%2d] T            = %3d\n"\
		  "# [%2d] Tstart       = %3d\n"\
                  "# [%2d] LX_global    = %3d\n"\
                  "# [%2d] LX           = %3d\n"\
		  "# [%2d] LXstart      = %3d\n"\
                  "# [%2d] LY_global    = %3d\n"\
                  "# [%2d] LY           = %3d\n"\
		  "# [%2d] LYstart      = %3d\n",
		  g_cart_id, g_cart_id, T_global,  g_cart_id, T,  g_cart_id, Tstart, 
                             g_cart_id, LX_global, g_cart_id, LX, g_cart_id, LXstart,
                             g_cart_id, LY_global, g_cart_id, LY, g_cart_id, LYstart);

  if(init_geometry() != 0) {
    fprintf(stderr, "ERROR from init_geometry\n");
#ifdef MPI
    MPI_Abort(MPI_COMM_WORLD, 2);
    MPI_Finalize();
#endif
    exit(1);
  }

  geometry();

  for(i = 0; i < 21; i++) isneg[i] = isneg_std[i];

  /* read the gauge field */
  alloc_gauge_field(&g_gauge_field, VOLUMEPLUSRAND);
  sprintf(filename, "%s.%.4d", gaugefilename_prefix, Nconf);
  if(g_cart_id==0) fprintf(stdout, "# reading gauge field from file %s\n", filename);
#ifdef MPI
  ratime = MPI_Wtime();
#else
  ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
  read_lime_gauge_field_doubleprec(filename);
#ifdef MPI
  retime = MPI_Wtime();
#else
  retime = (double)clock() / CLOCKS_PER_SEC;
#endif
 if(g_cart_id==0) fprintf(stdout, "# time to read gauge field: %e seconds\n", retime-ratime);



#ifdef MPI
  ratime = MPI_Wtime();
#else
  ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
  xchange_gauge();
#ifdef MPI
  retime = MPI_Wtime();
#else
  retime = (double)clock() / CLOCKS_PER_SEC;
#endif
 if(g_cart_id==0) fprintf(stdout, "# time to exchange gauge field: %e seconds\n", retime-ratime);

  /* measure the plaquette */
  plaquette(&plaq);
  if(g_cart_id==0) fprintf(stdout, "# measured plaquette value: %25.16e\n", plaq);

  if(Nlong>0 || N_Jacobi>0) {
    if(g_cart_id==0) {
      fprintf(stdout, "# apply fuzzing of gauge field and propagators with parameters:\n"\
                      "# Nlong = %d\n# N_ape = %d\n# alpha_ape = %f\n", Nlong, N_ape, alpha_ape);
    }
#ifdef MPI
    ratime = MPI_Wtime();
#else
    ratime = (double)clock() / CLOCKS_PER_SEC;
#endif

#if !( (defined PARALLELTX) || (defined PARALLELTXY) )
    alloc_gauge_field(&gauge_field_f, VOLUME);
    if( (gauge_field_timeslice = (double*)malloc(72*VOL3*sizeof(double))) == (double*)NULL  ) {
      fprintf(stderr, "Error, could not allocate mem for gauge_field_timeslice\n");
#ifdef MPI
      MPI_Abort(MPI_COMM_WORLD, 3);
      MPI_Finalize();
#endif
      exit(2);
    }
    for(x0=0; x0<T; x0++) {
      memcpy((void*)gauge_field_timeslice, (void*)(g_gauge_field+_GGI(g_ipt[x0][0][0][0],0)), 72*VOL3*sizeof(double));
      for(i=0; i<N_ape; i++) {
        APE_Smearing_Step_Timeslice(gauge_field_timeslice, alpha_ape);
      }
      if(Nlong > -1) {
        fuzzed_links_Timeslice(gauge_field_f, gauge_field_timeslice, Nlong, x0);
      } else {
        memcpy((void*)(gauge_field_f+_GGI(g_ipt[x0][0][0][0],0)), (void*)gauge_field_timeslice, 72*VOL3*sizeof(double));
      }
    }
    free(gauge_field_timeslice);
#else
    for(i=0; i<N_ape; i++) {
      APE_Smearing_Step(g_gauge_field, alpha_ape);
      xchange_gauge_field_timeslice(g_gauge_field);
    }

    alloc_gauge_field(&gauge_field_f, VOLUMEPLUSRAND);

    if(Nlong > 0) {
      fuzzed_links2(gauge_field_f, g_gauge_field, Nlong);
    } else {
      memcpy((void*)gauge_field_f, (void*)g_gauge_field, 72*VOLUMEPLUSRAND*sizeof(double));
    }
    xchange_gauge_field(gauge_field_f);

    read_lime_gauge_field_doubleprec(filename);
    xchange_gauge();
#endif

#ifdef MPI
    retime = MPI_Wtime();
#else
    retime = (double)clock() / CLOCKS_PER_SEC;
#endif
   if(g_cart_id==0) fprintf(stdout, "# time for smearing/fuzzing gauge field: %e seconds\n", retime-ratime);

  /* test: print the fuzzed APE smeared gauge field to stdout */
/*
    for(ix=0; ix<36*VOLUME; ix++) {
      fprintf(stdout, "%6d%25.16e%25.16e\n", ix, g_gauge_field[2*ix], g_gauge_field[2*ix+1]);
    }
*/
  }

  /* allocate memory for the spinor fields */
  no_fields = 8;
  no_fields *= n_c;
  hidx = n_s * n_c;
  if(Nlong>0) {
    no_fields += 4*n_c;
    hidx = 2 * n_s * n_c;
  } else if(N_ape>0) {
    no_fields += 12*n_c;
    hidx = 4 * n_s * n_c;
  }
  no_fields++;
  g_spinor_field = (double**)calloc(no_fields, sizeof(double*));
#if !( (defined PARALLELTX) || (defined PARALLELTXY) )
/*
  for(i=0; i<no_fields-1; i++) alloc_spinor_field(&g_spinor_field[i], VOLUME);
  alloc_spinor_field(&g_spinor_field[no_fields-1], VOLUME + RAND);
*/
  for(i=0; i<no_fields; i++) alloc_spinor_field(&g_spinor_field[i], VOLUME + RAND);
#else
  for(i=0; i<no_fields; i++) alloc_spinor_field(&g_spinor_field[i], VOLUME + RAND);
#endif

  /* allocate memory for the contractions */
#if (defined PARALLELTX) || (defined PARALLELTXY)
  if(g_xs_id==0) { idx = 8*K*T_global; } 
  else           { idx = 8*K*T; }
#else
  if(g_cart_id==0) { idx = 8*K*T_global; } 
  else             { idx = 8*K*T; }
#endif
  cconn = (double*)calloc(idx, sizeof(double));
  if( cconn==(double*)NULL ) {
    fprintf(stderr, "could not allocate memory for cconn\n");
#ifdef MPI
    MPI_Abort(MPI_COMM_WORLD, 4);
    MPI_Finalize();
#endif
    exit(3);
  }
  for(ix=0; ix<idx; ix++) cconn[ix] = 0.;

#ifdef MPI
  buffer = (double*)calloc(idx, sizeof(double));
  if( buffer==(double*)NULL ) {
    fprintf(stderr, "could not allocate memory for buffer\n");
    MPI_Abort(MPI_COMM_WORLD, 5);
    MPI_Finalize();
    exit(4);
  }
#endif

  if( (Ctmp = (double*)calloc(2*T, sizeof(double))) == NULL ) {
    fprintf(stderr, "Error, could not allocate mem for Ctmp\n");
#ifdef MPI
    MPI_Abort(MPI_COMM_WORLD, 6);
    MPI_Finalize();
#endif
    exit(5);
  }

  /*********************************************
   * read the extra masses for mms
   * - if mms is not used, use g_mu as the mass 
   *********************************************/
  if( (mms_masses = (double*)calloc(g_no_extra_masses+1, sizeof(double))) == NULL ) {
    fprintf(stderr, "Error, could allocate mms_masses\n");
#ifdef MPI
    MPI_Abort(MPI_COMM_WORLD, 7);
    MPI_Finalize();
#endif
    exit(6);
  }
  mms_masses[0] = g_mu;
  if(g_no_extra_masses>0) {
    if( (ofs=fopen(mms_extra_masses_file, "r"))==NULL ) {
      fprintf(stderr, "Error, could not open file %s for reading\n", mms_extra_masses_file);
#ifdef MPI
      MPI_Abort(MPI_COMM_WORLD, 8);
      MPI_Finalize();
#endif
      exit(7);
    }
    for(i=0; i<g_no_extra_masses; i++) fscanf(ofs, "%lf", mms_masses+i+1);
    fclose(ofs);
  }
  if(g_cart_id==0) {
    fprintf(stdout, "# mms masses:\n");
    for(i=0; i<=g_no_extra_masses; i++) fprintf(stdout, "# mass[%2d] = %e\n",
      i, mms_masses[i]);
  }
  timeslice = g_source_timeslice;

#ifdef _MMS_MASS_DIAGONAL
  /****************************************************************
   * (1.0) loop on the mass-diagonal correlators mms1 = mms2 = 0
   *       (light mass only)
   ****************************************************************/
  mms1 = 0;
  mms2 = 0;
  count=-1;
  for(sigmalight = -1; sigmalight <= 1; sigmalight+=2) {
  for(sigmaheavy = -1; sigmaheavy <= 1; sigmaheavy+=2) {
    if(sigmalight == sigmaheavy) {
      xgindex1 = gindex1;  xgindex2 = gindex2;  xisimag=isimag;  xvsign=vsign;  conf_gamma_sign = c_conf_gamma_sign;
    } else {
      xgindex1 = ngindex1; xgindex2 = ngindex2; xisimag=nisimag; xvsign=nvsign; conf_gamma_sign = n_conf_gamma_sign;
    }
    count++;
    for(j=0; j<8*K*T; j++) cconn[j] = 0.;
    mulight = (double)sigmalight * mms_masses[mms1];
    muheavy = (double)sigmaheavy * mms_masses[mms2];
    /*************************************
     * begin loop on LL, LS, SL, SS
     *************************************/
    ll = 0;
    for(j=0; j<1; j++) {
      work = g_spinor_field[no_fields-1];
      if(j==0) {
        /* local-local (source-sink) -> phi[0-3]^dagger.p[0-3] -> p.p */
        ll = 0;
        for(i=0; i<n_s*n_c; i++) {
          if(light_nomms) {
            if(sigmalight==-1) 
              sprintf(filename, "%s/source.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i);
            else
              sprintf(filename, "%s/msource.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i);
            read_lime_spinor(g_spinor_field[i], filename, 0);
            if(sigmaheavy==-1) 
              sprintf(filename, "%s/source.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i);
             else
              sprintf(filename, "%s/msource.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i);
            read_lime_spinor(g_spinor_field[i+n_s*n_c], filename, 0);
          } else {
            sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i, mms1);
            read_lime_spinor(work, filename, 0);
            xchange_field(work);
            Qf5(g_spinor_field[i], work, mulight);
            Qf5(g_spinor_field[i+n_s*n_c], work, muheavy);
          }
        }
        chi  = &g_spinor_field[0];
        psi  = &g_spinor_field[n_s*n_c];
      } else if(j==1) {
        if(Nlong>-1) {
          /* fuzzed-local -> phi[0-3]^dagger.phi[4-7] -> p.f */
          ll = 2;
          chi = &g_spinor_field[0];
          psi = &g_spinor_field[n_s*n_c];
          for(i=n_s*n_c; i<2*n_s*n_c; i++) {
            if(light_nomms) {
              if(sigmaheavy==-1)
                sprintf(filename, "%s/source.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i);
              else 
                sprintf(filename, "%s/msource.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i);
              read_lime_spinor(g_spinor_field[i], filename, 0);
            } else {
              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i, mms2);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i], work, muheavy);
            }
          }
        } else {
          /* local-smeared */
          ll = 1; 
          chi = &g_spinor_field[0];
          psi = &g_spinor_field[n_s*n_c];
          for(i = 0; i < 2*n_s*n_c; i++) {
            xchange_field_timeslice(g_spinor_field[i]);
            for(c=0; c<N_Jacobi; c++) {
              Jacobi_Smearing_Step_one(gauge_field_f, g_spinor_field[i], work, kappa_Jacobi);
              xchange_field_timeslice(g_spinor_field[i]);
            }
          }
        }
      } else if(j==2) {
        if(Nlong>-1) {
          /* local-fuzzed -> phi[0-3]^dagger.phi[4-7] -> p.pf */
          ll = 1;
          chi  = &g_spinor_field[0];
          psi  = &g_spinor_field[n_s*n_c];
          for(i=0; i<n_s*n_c; i++) {
            if(light_nomms) {
              if(sigmaheavy==-1)
                sprintf(filename, "%s/source.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i);
              else
                sprintf(filename, "%s/msource.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i);
              read_lime_spinor(g_spinor_field[i+n_s*n_c], filename, 0);
            } else {
              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i, mms2);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i+n_s*n_c], work, muheavy);
            }
            if(g_cart_id==0) fprintf(stdout, "# fuzzing prop. with Nlong=%d, N_APE=%d, alpha_APE=%f\n",
                               Nlong, N_ape, alpha_ape);
            xchange_field_timeslice(g_spinor_field[i+n_s*n_c]);
            Fuzz_prop3(gauge_field_f, g_spinor_field[i+n_s*n_c], work, Nlong);
          }
        } else {
          /* smeared-local */
          ll = 2;
          chi = &g_spinor_field[0];
          psi = &g_spinor_field[n_s*n_c];
          for(i=0; i<n_s*n_c; i++) {
            if(light_nomms) {
              if(sigmalight==-1)
                sprintf(filename, "%s/source.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i+n_s*n_c);
              else
                sprintf(filename, "%s/msource.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i+n_s*n_c);
              read_lime_spinor(g_spinor_field[i], filename, 0);

              if(sigmaheavy==-1)
                sprintf(filename, "%s/source.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i+n_s*n_c);
              else
                sprintf(filename, "%s/msource.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i+n_s*n_c);
              read_lime_spinor(g_spinor_field[i+n_s*n_c], filename, 0);
            } else {
              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i+n_s*n_c, mms1);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i],         work, mulight);
              Qf5(g_spinor_field[i+n_s*n_c], work, muheavy);
            }
          }
        }
      } else if(j==3) {
        ll = 3;
        if(Nlong>-1) {
          /* fuzzed-fuzzed -> phi[0-3]^dagger.phi[4-7] -> f.pf */
          chi = &g_spinor_field[0];
          psi = &g_spinor_field[n_s*n_c];
          for(i=0; i<n_s*n_c; i++) {
            if(light_nomms) {
              if(sigmalight==-1)
                sprintf(filename, "%s/source.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i+n_s*n_c);
              else
                sprintf(filename, "%s/msource.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i+n_s*n_c);
              read_lime_spinor(g_spinor_field[i], filename, 0);
            } else {
              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i+n_s*n_c, mms1);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i], work, mulight);
            }
          }
        } else {
          /* smeared-smeared -> phi[0-3]^dagger.phi[4-7] -> f.pf */
          chi = &g_spinor_field[0];
          psi = &g_spinor_field[n_s*n_c];
          for(i = 0; i < 2*n_s*n_c; i++) {
            xchange_field_timeslice(g_spinor_field[i]);
            for(c=0; c<N_Jacobi; c++) {
              Jacobi_Smearing_Step_one(gauge_field_f, g_spinor_field[i], work, kappa_Jacobi);
              xchange_field_timeslice(g_spinor_field[i]);
            }
          }
        }
      }

      /************************************************************
       * the charged contractions
       ************************************************************/
#ifdef MPI
      ratime = MPI_Wtime();
#else
      ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
      sl = 2*ll*T*K;
      itype = 1; 
      /* pion sector */
      for(idx=0; idx<9; idx++) {
        contract_twopoint(&cconn[sl], xgindex1[idx], xgindex2[idx], chi, psi, n_c);
/*        for(x0=0; x0<T; x0++) fprintf(stdout, "pion: %3d%25.16e%25.16e\n", x0, 
          cconn[sl+2*x0]/(double)VOL3/2./g_kappa/g_kappa, cconn[sl+2*x0+1]/(double)VOL3/2./g_kappa/g_kappa); */
        sl += (2*T);        
        itype++; 
      }

      /* rho sector */
      for(idx = 9; idx < 36; idx+=3) {
        for(i = 0; i < 3; i++) {
          for(x0=0; x0<2*T; x0++) Ctmp[x0] = 0.;
          contract_twopoint(Ctmp, xgindex1[idx+i], xgindex2[idx+i], chi, psi, n_c);
          for(x0=0; x0<T; x0++) {
            cconn[sl+2*x0  ] += (conf_gamma_sign[(idx-9)/3]*xvsign[idx-9+i]*Ctmp[2*x0  ]);
            cconn[sl+2*x0+1] += (conf_gamma_sign[(idx-9)/3]*xvsign[idx-9+i]*Ctmp[2*x0+1]);
          }
/*
            for(x0=0; x0<T; x0++) {
              x1 = (x0+timeslice)%T_global;
              fprintf(stdout, "rho: %3d%25.16e%25.16e\n", x0, 
                xvsign[idx-9+i]*Ctmp[2*x1  ]/(double)VOL3/2./g_kappa/g_kappa, 
                xvsign[idx-9+i]*Ctmp[2*x1+1]/(double)VOL3/2./g_kappa/g_kappa);
            }
*/
        }
        sl += (2*T); 
        itype++;
      }
    
      /* the a0 */
      contract_twopoint(&cconn[sl], xgindex1[36], xgindex2[36], chi, psi, n_c);
      sl += (2*T);
      itype++;

      /* the b1 */
      for(i=0; i<3; i++) {
        for(x0=0; x0<2*T; x0++) Ctmp[x0] = 0.;
        idx = 37;
        contract_twopoint(Ctmp, xgindex1[idx+i], xgindex2[idx+i], chi, psi, n_c);
        for(x0=0; x0<T; x0++) { 
          cconn[sl+2*x0  ] += (xvsign[idx-9+i]*Ctmp[2*x0  ]);
          cconn[sl+2*x0+1] += (xvsign[idx-9+i]*Ctmp[2*x0+1]);
        }
      }

      if(j==0) {
        for(i=0; i<n_s*n_c; i++) {
          xchange_field(g_spinor_field[i]);
          xchange_field(g_spinor_field[i+n_s*n_c]);
        }
        sl += (2*T);
        contract_cvc(&cconn[sl], 5, 0, chi, psi, n_c);
        if(sigmalight == -1 && sigmaheavy == -1) {
          contract_cvc_contact_term(&contact_term, chi, n_c, NULL);
/*          contract_cvc_contact_term(&contact_term, chi, n_c, work); */
        }
      }

#ifdef MPI
      retime = MPI_Wtime();
#else
      retime = (double)clock() / CLOCKS_PER_SEC;
#endif
      if(g_cart_id==0) fprintf(stdout, "# time for contraction j=%d: %e seconds\n", j, retime-ratime);
    }  /* of j=0,...,3 */

#ifdef MPI
    ratime = MPI_Wtime();
#else
    ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
#ifdef MPI
    /* collect the results */
#if (defined PARALLELTX) || (defined PARALLELTXY)
    MPI_Allreduce(cconn, buffer, 8*K*T, MPI_DOUBLE, MPI_SUM, g_ts_comm);
    MPI_Gather(buffer, 8*K*T, MPI_DOUBLE, cconn, 8*K*T, MPI_DOUBLE, 0, g_xs_comm);
#else
    MPI_Gather(cconn, 8*K*T, MPI_DOUBLE, buffer, 8*K*T, MPI_DOUBLE, 0, g_cart_grid);
    if(g_cart_id==0) memcpy((void*)cconn, (void*)buffer, 8*K*T_global*sizeof(double));
#endif
#endif
    /* write/add to file */
    if(g_cart_id==0) {
      sprintf(filename, "correl.%.4d.%.2d.%.2d.%.2d", Nconf, timeslice, mms1, mms2);
      if(count==0) {
        ofs=fopen(filename, "w");
      } else {
        ofs=fopen(filename, "a");
      }
      if( ofs == (FILE*)NULL ) {
        fprintf(stderr, "Error, could not open file %s for writing\n", filename);
#ifdef MPI
        MPI_Abort(MPI_COMM_WORLD, 9);
        MPI_Finalize();
#endif
        exit(8);
      }
      fprintf(stdout, "# writing correlators to file %s\n", filename);
      fprintf(ofs, "# %5d%3d%3d%3d%3d%15.8e%15.8e%15.8e%3d%3d\n", 
        Nconf, T_global, LX_global, LY_global, LZ, g_kappa, mms_masses[mms1], mms_masses[mms2], -sigmalight, -sigmaheavy);
      for(idx=0; idx<K; idx++) {
        for(ll=0; ll<4; ll++) {
          x1 = (0+timeslice) % T_global;
          i = 2* ( (x1/T)*4*K*T + ll*K*T + idx*T + x1%T ) + xisimag[idx];
          fprintf(ofs, "%3d%3d%4d%25.16e%25.16e\n", idx+1, 2*ll+1, 0,
            isneg[idx]*cconn[i]/(VOL3*g_nproc_x*g_nproc_y)/g_kappa/g_kappa/2., 0.);
          for(x0=1; x0<T_global/2; x0++) {
            x1 = ( x0+timeslice) % T_global;
            x2 = (-x0+timeslice+T_global) % T_global;
            i = 2* ( (x1/T)*4*K*T + ll*K*T + idx*T + x1%T ) + xisimag[idx];
            j = 2* ( (x2/T)*4*K*T + ll*K*T + idx*T + x2%T ) + xisimag[idx];
/*            fprintf(stdout, "idx=%d; x0=%d, x1=%d, x2=%d, i=%d, j=%d\n", idx, x0, x1, x2, i, j); */
            fprintf(ofs, "%3d%3d%4d%25.16e%25.16e\n", idx+1, 2*ll+1, x0,
              isneg[idx]*cconn[i]/(VOL3*g_nproc_x*g_nproc_y)/g_kappa/g_kappa/2., isneg[idx]*cconn[j]/(VOL3*g_nproc_x*g_nproc_y)/g_kappa/g_kappa/2.); 
          }
          x0 = T_global/2;
          x1 = (x0+timeslice) % T_global;
          i = 2* ( (x1/T)*4*K*T + ll*K*T + idx*T + x1%T ) + xisimag[idx];
          fprintf(ofs, "%3d%3d%4d%25.16e%25.16e\n", idx+1, 2*ll+1, x0,
            isneg[idx]*cconn[i]/(VOL3*g_nproc_x*g_nproc_y)/g_kappa/g_kappa/2., 0.);
        }
      }    
      fclose(ofs);
    }  /* of if g_cart_id == 0 */
#ifdef MPI
    retime = MPI_Wtime();
#else
    retime = (double)clock() / CLOCKS_PER_SEC;
#endif
    if(g_cart_id==0) fprintf(stdout, "# time for collecting/writing contractions: %e seconds\n", retime-ratime);

  }}  /* of sigmalight/sigmaheavy */

  /****************************************************************
   * (1.1) loop on the mass-diagonal correlators mms1 = mms2 = k
   *       for 1 \le k \le g_no_extra_masses, start with mass mms2_min
   ****************************************************************/
  for(k=mms2_min; k<=g_no_extra_masses; k++) {  
    mms1 = k; 
    mms2 = k;
    if(light_nomms) {mms1 -= 1; mms2 -= 1;}
    count = -1;
    for(sigmalight=-1; sigmalight<=1; sigmalight+=2) {
/*    for(sigmalight=-1; sigmalight<=-1; sigmalight+=2) { */
    for(sigmaheavy=-1; sigmaheavy<=1; sigmaheavy+=2) {
      if(sigmalight == sigmaheavy) {
        xgindex1 = gindex1;  xgindex2 = gindex2;  xisimag=isimag;  xvsign=vsign;  conf_gamma_sign = c_conf_gamma_sign;
      } else {
        xgindex1 = ngindex1; xgindex2 = ngindex2; xisimag=nisimag; xvsign=nvsign; conf_gamma_sign = n_conf_gamma_sign;
      }
      count++;
      for(j=0; j<8*K*T; j++) cconn[j] = 0.;
      mulight = (double)sigmalight * mms_masses[k];
      muheavy = (double)sigmaheavy * mms_masses[k];
      /*************************************
       * begin loop on LL, LS, SL, SS
       *************************************/
      ll = 0;
      for(j=0; j<4; j++) {
        work = g_spinor_field[no_fields-1];
        if(j==0) {
          /* local-local (source-sink) -> phi[0-3]^dagger.p[0-3] -> p.p */
          ll = 0;
          for(i=0; i<n_s*n_c; i++) {
            sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i, mms1);
#ifdef MPI
            ratime = MPI_Wtime();
#else
            ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
            read_lime_spinor(work, filename, 0);
#ifdef MPI
            retime = MPI_Wtime();
#else
            retime = (double)clock() / CLOCKS_PER_SEC;
#endif
            if(g_cart_id==0) fprintf(stdout, "# time to read spinor field: %e seconds\n", retime-ratime);

#ifdef MPI
            ratime = MPI_Wtime();
#else
            ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
            xchange_field(work);
#ifdef MPI
            retime = MPI_Wtime();
#else
            retime = (double)clock() / CLOCKS_PER_SEC;
#endif
            if(g_cart_id==0) fprintf(stdout, "# time to exchange spinor field: %e seconds\n", retime-ratime);
            Qf5(g_spinor_field[i], work, mulight);
            Qf5(g_spinor_field[i+n_s*n_c], work, muheavy);
          }
          chi  = &g_spinor_field[0];
          psi  = &g_spinor_field[n_s*n_c];
        } else if(j==1) {
          if(Nlong>-1) {
            /* fuzzed-local -> phi[0-3]^dagger.phi[4-7] -> p.f */
            ll = 2;
            chi = &g_spinor_field[0];
            psi = &g_spinor_field[n_s*n_c];
            for(i=n_s*n_c; i<2*n_s*n_c; i++) {
              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i, mms2);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i], work, muheavy);
            }
          } else {
            /* local-smeared */
            ll = 1; 
            chi = &g_spinor_field[0];
            psi = &g_spinor_field[n_s*n_c];
            for(i = 0; i < 2*n_s*n_c; i++) {
              xchange_field_timeslice(g_spinor_field[i]);
              for(c=0; c<N_Jacobi; c++) {
                Jacobi_Smearing_Step_one(gauge_field_f, g_spinor_field[i], work, kappa_Jacobi);
                xchange_field_timeslice(g_spinor_field[i]);
              }
            }
          }
        } else if(j==2) {
          if(Nlong>-1) {
            /* local-fuzzed -> phi[0-3]^dagger.phi[4-7] -> p.pf */
            ll = 1;
            chi  = &g_spinor_field[0];
            psi  = &g_spinor_field[n_s*n_c];
            for(i=0; i<n_s*n_c; i++) {
              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i, mms2);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i+n_s*n_c], work, muheavy);
              if(g_cart_id==0) fprintf(stdout, "# fuzzing prop. with Nlong=%d, N_APE=%d, alpha_APE=%f\n",
                                 Nlong, N_ape, alpha_ape);
              xchange_field_timeslice(g_spinor_field[i+n_s*n_c]);
#ifdef MPI
              ratime = MPI_Wtime();
#else
              ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
              Fuzz_prop3(gauge_field_f, g_spinor_field[i+n_s*n_c], work, Nlong);
#ifdef MPI
              retime = MPI_Wtime();
#else
              retime = (double)clock() / CLOCKS_PER_SEC;
#endif
              if(g_cart_id==0) fprintf(stdout, "# time to fuzz spinor field no. %d: %e seconds\n", i+n_s*n_c, retime-ratime);
            }
          } else {
            /* smeared-local */
            ll = 2;
            chi = &g_spinor_field[0];
            psi = &g_spinor_field[n_s*n_c];
            for(i=0; i<n_s*n_c; i++) {
              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i+n_s*n_c, mms1);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i], work, mulight);

              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i+n_s*n_c, mms2);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i+n_s*n_c], work, muheavy);
            }
          }
        } else if(j==3) {
          ll = 3;
          if(Nlong>-1) {
            /* fuzzed-fuzzed -> phi[0-3]^dagger.phi[4-7] -> f.pf */
            chi = &g_spinor_field[0];
            psi = &g_spinor_field[n_s*n_c];
            for(i=0; i<n_s*n_c; i++) {
              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i+n_s*n_c, mms1);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i], work, mulight);
            }
          } else {
            /* smeared-smeared -> phi[0-3]^dagger.phi[4-7] -> f.pf */
            chi = &g_spinor_field[0];
            psi = &g_spinor_field[n_s*n_c];
            for(i = 0; i < 2*n_s*n_c; i++) {
              xchange_field_timeslice(g_spinor_field[i]);
              for(c=0; c<N_Jacobi; c++) {
                Jacobi_Smearing_Step_one(gauge_field_f, g_spinor_field[i], work, kappa_Jacobi);
                xchange_field_timeslice(g_spinor_field[i]);
              }
            }
          }
        }

        /************************************************************
         * the charged contractions
         ************************************************************/
#ifdef MPI
        ratime = MPI_Wtime();
#else
        ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
        sl = 2*ll*T*K;
        itype = 1; 
        /* pion sector */
        for(idx=0; idx<9; idx++) {
          contract_twopoint(&cconn[sl], xgindex1[idx], xgindex2[idx], chi, psi, n_c);
/*          for(x0=0; x0<T; x0++) fprintf(stdout, "pion: %3d%25.16e%25.16e\n", x0, 
            cconn[sl+2*x0]/(double)VOL3/2./g_kappa/g_kappa, cconn[sl+2*x0+1]/(double)VOL3/2./g_kappa/g_kappa); */
          sl += (2*T);        
          itype++; 
        }

        /* rho sector */
        for(idx = 9; idx < 36; idx+=3) {
          for(i = 0; i < 3; i++) {
            for(x0=0; x0<2*T; x0++) Ctmp[x0] = 0.;
            contract_twopoint(Ctmp, xgindex1[idx+i], xgindex2[idx+i], chi, psi, n_c);
            for(x0=0; x0<T; x0++) {
              cconn[sl+2*x0  ] += (conf_gamma_sign[(idx-9)/3]*xvsign[idx-9+i]*Ctmp[2*x0  ]);
              cconn[sl+2*x0+1] += (conf_gamma_sign[(idx-9)/3]*xvsign[idx-9+i]*Ctmp[2*x0+1]);
            }
/*
            for(x0=0; x0<T; x0++) {
              x1 = (x0+timeslice)%T_global;
              fprintf(stdout, "rho: %3d%25.16e%25.16e\n", x0, 
                xvsign[idx-9+i]*Ctmp[2*x1  ]/(double)VOL3/2./g_kappa/g_kappa, 
                xvsign[idx-9+i]*Ctmp[2*x1+1]/(double)VOL3/2./g_kappa/g_kappa);
            }
*/
          }
          sl += (2*T); 
          itype++;
        }
    
        /* the a0 */
        contract_twopoint(&cconn[sl], xgindex1[36], xgindex2[36], chi, psi, n_c);
        sl += (2*T);
        itype++;

        /* the b1 */
        for(i=0; i<3; i++) {
          for(x0=0; x0<2*T; x0++) Ctmp[x0] = 0.;
          idx = 37;
          contract_twopoint(Ctmp, xgindex1[idx+i], xgindex2[idx+i], chi, psi, n_c);
          for(x0=0; x0<T; x0++) { 
            cconn[sl+2*x0  ] += (xvsign[idx-9+i]*Ctmp[2*x0  ]);
            cconn[sl+2*x0+1] += (xvsign[idx-9+i]*Ctmp[2*x0+1]);
          }
        }
#ifdef MPI
        retime = MPI_Wtime();
#else
        retime = (double)clock() / CLOCKS_PER_SEC;
#endif
        if(g_cart_id==0) fprintf(stdout, "# time for contraction j=%d: %e seconds\n", j, retime-ratime);
      }  /* of j=0,...,3 */

#ifdef MPI
      ratime = MPI_Wtime();
#else
      ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
#ifdef MPI
      /* collect the results */
#if (defined PARALLELTX) || (defined PARALLELTXY)
      MPI_Allreduce(cconn, buffer, 8*K*T, MPI_DOUBLE, MPI_SUM, g_ts_comm);
      MPI_Gather(buffer, 8*K*T, MPI_DOUBLE, cconn, 8*K*T, MPI_DOUBLE, 0, g_xs_comm);
#else
      MPI_Gather(cconn, 8*K*T, MPI_DOUBLE, buffer, 8*K*T, MPI_DOUBLE, 0, g_cart_grid);
      if(g_cart_id==0) memcpy((void*)cconn, (void*)buffer, 8*K*T_global*sizeof(double));
#endif
#endif
      /* write/add to file */
      if(g_cart_id==0) {
        sprintf(filename, "correl.%.4d.%.2d.%.2d.%.2d", Nconf, timeslice, k, k);
        if(count==0) {
          ofs=fopen(filename, "w");
        } else {
          ofs=fopen(filename, "a");
        }
        if( ofs == (FILE*)NULL ) {
          fprintf(stderr, "Error, could not open file %s for writing\n", filename);
#ifdef MPI
          MPI_Abort(MPI_COMM_WORLD, 9);
          MPI_Finalize();
#endif
          exit(8);
        }
        fprintf(stdout, "# writing correlators to file %s\n", filename);
        fprintf(ofs, "# %5d%3d%3d%3d%3d%15.8e%15.8e%15.8e%3d%3d\n", 
          Nconf, T_global, LX_global, LY_global, LZ, g_kappa, mms_masses[k], mms_masses[k], -sigmalight, -sigmaheavy);
        for(idx=0; idx<K; idx++) {
          for(ll=0; ll<4; ll++) {
            x1 = (0+timeslice) % T_global;
            i = 2* ( (x1/T)*4*K*T + ll*K*T + idx*T + x1%T ) + xisimag[idx];
            fprintf(ofs, "%3d%3d%4d%25.16e%25.16e\n", idx+1, 2*ll+1, 0,
              isneg[idx]*cconn[i]/(VOL3*g_nproc_x*g_nproc_y)/g_kappa/g_kappa/2., 0.);
            for(x0=1; x0<T_global/2; x0++) {
              x1 = ( x0+timeslice) % T_global;
              x2 = (-x0+timeslice+T_global) % T_global;
              i = 2* ( (x1/T)*4*K*T + ll*K*T + idx*T + x1%T ) + xisimag[idx];
              j = 2* ( (x2/T)*4*K*T + ll*K*T + idx*T + x2%T ) + xisimag[idx];
/*              fprintf(stdout, "idx=%d; x0=%d, x1=%d, x2=%d, i=%d, j=%d\n", idx, x0, x1, x2, i, j); */
              fprintf(ofs, "%3d%3d%4d%25.16e%25.16e\n", idx+1, 2*ll+1, x0,
                isneg[idx]*cconn[i]/(VOL3*g_nproc_x*g_nproc_y)/g_kappa/g_kappa/2., isneg[idx]*cconn[j]/(VOL3*g_nproc_x*g_nproc_y)/g_kappa/g_kappa/2.); 
            }
            x0 = T_global/2;
            x1 = (x0+timeslice) % T_global;
            i = 2* ( (x1/T)*4*K*T + ll*K*T + idx*T + x1%T ) + xisimag[idx];
            fprintf(ofs, "%3d%3d%4d%25.16e%25.16e\n", idx+1, 2*ll+1, x0,
              isneg[idx]*cconn[i]/(VOL3*g_nproc_x*g_nproc_y)/g_kappa/g_kappa/2., 0.);
          }
        }    
        fclose(ofs);
      }  /* of if g_cart_id == 0 */
#ifdef MPI
      retime = MPI_Wtime();
#else
      retime = (double)clock() / CLOCKS_PER_SEC;
#endif
      if(g_cart_id==0) fprintf(stdout, "# time for collecting/writing contractions: %e seconds\n", retime-ratime);

    }}  /* of sigmalight/sigmaheavy */
  }  /* of i=0,...,g_no_extra_masses */
#endif

#ifdef _MMS_LIGHT_ALL_HEAVY
  /**************************************************************************
   * (2) loop on the mms extra masses
   * - mms1 is fixed to the light quark mass, i.e. mms1 = 0
   * - mms2 runs through the extra masses, 1 \le mms2 \le g_no_extra_masses
   **************************************************************************/
  mms1 = 0; 
/* for(mms1=0; mms1<mms2_min; mms1++) { */
  count = -1; 
  for(sigmalight=-1; sigmalight<=1; sigmalight+=2) {

    mulight = (double)sigmalight * mms_masses[mms1];
    /*************************************
     * read in L, S for the light mass
     *************************************/
    work = g_spinor_field[no_fields-1];
    for(j=0; j<4; j++) {
      if(j==0) {
        /* local-local (source-sink) -> phi[0-3]^dagger.p[0-3] -> p.p */
        for(i=0; i<n_s*n_c; i++) {
          if( (mms1==0) && light_nomms) {
            if(sigmalight==-1)
              sprintf(filename, "%s/source.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i);
            else
              sprintf(filename, "%s/msource.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i);
            read_lime_spinor(g_spinor_field[i], filename, 0);
          } else if( (mms1>0) && light_nomms) {
            sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i, mms1-1);
            read_lime_spinor(work, filename, 0);
            xchange_field(work);
            Qf5(g_spinor_field[i], work, mulight);
          } else {
            sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i, mms1);
            read_lime_spinor(work, filename, 0);
            xchange_field(work);
            Qf5(g_spinor_field[i], work, mulight);
          }
        }
      } else if(j==1) {
        if(Nlong>-1) {
          /* fuzzed-local -> phi[0-3]^dagger.phi[4-7] -> p.f */
          continue;
        } else {
          /* local-smeared */
          for(i = 0; i < n_s*n_c; i++) {
            memcpy((void*)g_spinor_field[i+n_s*n_c], (void*)g_spinor_field[i], 24*VOLUMEPLUSRAND*sizeof(double));
            xchange_field_timeslice(g_spinor_field[i+n_s*n_c]);
            for(c=0; c<N_Jacobi; c++) {
              Jacobi_Smearing_Step_one(gauge_field_f, g_spinor_field[i+n_s*n_c], work, kappa_Jacobi);
              xchange_field_timeslice(g_spinor_field[i+n_s*n_c]);
            }
          }
        }
      } else if(j==2) {
        if(Nlong>-1) {
          /* local-fuzzed -> phi[0-3]^dagger.phi[4-7] -> p.pf */
          continue;
        } else {
          /* smeared-local */
          for(i=0; i<n_s*n_c; i++) {
            if( (mms1==0) && light_nomms) {
              if(sigmalight==-1)
                sprintf(filename, "%s/source.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i+n_s*n_c);
              else
                sprintf(filename, "%s/msource.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i+n_s*n_c);
              read_lime_spinor(g_spinor_field[i+2*n_s*n_c], filename, 0);
            } else if( (mms1>0) && light_nomms) {
              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i+n_s*n_c, mms1-1);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i+2*n_s*n_c], work, mulight);
            } else {
              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i+n_s*n_c, mms1);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i+2*n_s*n_c], work, mulight);
            }
          }
        }
      } else if(j==3) {
        if(Nlong>-1) {
          /* fuzzed-fuzzed -> phi[0-3]^dagger.phi[4-7] -> f.pf */
          for(i=0; i<n_s*n_c; i++) {
            if( (mms1==0) && light_nomms) {
              if(sigmalight==-1)
                sprintf(filename, "%s/source.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i+n_s*n_c);
              else
                sprintf(filename, "%s/msource.%.4d.%.2d.%.2d.inverted", filename_prefix2, Nconf, timeslice, i+n_s*n_c);
              read_lime_spinor(g_spinor_field[i+n_s*n_c], filename, 0);
            } else if( (mms1>0) && light_nomms) {
              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i+n_s*n_c, mms1-1);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i+n_s*n_c], work, mulight);
            } else {
              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i+n_s*n_c, mms1);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i+n_s*n_c], work, mulight);
            }
          }
        } else {
          /* smeared-smeared -> phi[0-3]^dagger.phi[4-7] -> f.pf */
          for(i = 0; i < n_s*n_c; i++) {
            memcpy((void*)g_spinor_field[i+3*n_s*n_c], (void*)g_spinor_field[i+2*n_s*n_c], 24*VOLUMEPLUSRAND*sizeof(double));
            xchange_field_timeslice(g_spinor_field[i+3*n_s*n_c]);
            for(c=0; c<N_Jacobi; c++) {
              Jacobi_Smearing_Step_one(gauge_field_f, g_spinor_field[i+3*n_s*n_c], work, kappa_Jacobi);
              xchange_field_timeslice(g_spinor_field[i+3*n_s*n_c]);
            }
          }
        }
      }
    }  /* of j=0,...,3 for light */

  for(sigmaheavy=-1; sigmaheavy<=1; sigmaheavy+=2) {
    if(sigmalight == sigmaheavy) {
      xgindex1 = gindex1;  xgindex2 = gindex2;  xisimag=isimag;  xvsign=vsign;  conf_gamma_sign = c_conf_gamma_sign;
    } else {
      xgindex1 = ngindex1; xgindex2 = ngindex2; xisimag=nisimag; xvsign=nvsign; conf_gamma_sign = n_conf_gamma_sign;
    }
    count++;

    /***************************************************
     * begin loop on the heavy masses
     **************************************************/
    work = g_spinor_field[no_fields-1];
    for(k=mms2_min; k<=g_no_extra_masses; k++) {
      mms2 = k;
      if(light_nomms) mms2 -= 1;
      for(j=0; j<8*K*T; j++) cconn[j] = 0.;
      muheavy = (double)sigmaheavy * mms_masses[k];

      /***************************************************
       * begin loop on LL, LS, SL, SS for the heavy mass
       **************************************************/
      ll = 0;
      for(j=0; j<4; j++) {
        if(j==0) {
          /* local-local (source-sink) -> phi[0-3]^dagger.p[0-3] -> p.p */
          ll = 0;
          for(i=0; i<n_s*n_c; i++) {
            sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i, mms2);
            read_lime_spinor(work, filename, 0);
            xchange_field(work);
            Qf5(g_spinor_field[i+hidx], work, muheavy);
          }
          chi  = &g_spinor_field[0];
          psi  = &g_spinor_field[hidx];
        } else if(j==1) {
          if(Nlong>-1) {
            /* fuzzed-local -> phi[0-3]^dagger.phi[4-7] -> p.f */
            ll = 2;
            chi = &g_spinor_field[0];
            psi = &g_spinor_field[hidx];
            for(i=0; i<n_s*n_c; i++) {
              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i+n_s*n_c, mms2);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i+hidx], work, muheavy);
            }
          } else {
            /* local-smeared */
            ll = 1; 
            chi = &g_spinor_field[n_s*n_c];
            psi = &g_spinor_field[hidx];
            for(i = 0; i < n_s*n_c; i++) {
              xchange_field_timeslice(g_spinor_field[i+hidx]);
              for(c=0; c<N_Jacobi; c++) {
                Jacobi_Smearing_Step_one(gauge_field_f, g_spinor_field[i+hidx], work, kappa_Jacobi);
                xchange_field_timeslice(g_spinor_field[i+hidx]);
              }
            }
          }
        } else if(j==2) {
          if(Nlong>-1) {
            /* local-fuzzed -> phi[0-3]^dagger.phi[4-7] -> p.pf */
            ll = 1;
            chi  = &g_spinor_field[0];
            psi  = &g_spinor_field[hidx];
            for(i=0; i<n_s*n_c; i++) {
              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i, mms2);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i+hidx], work, muheavy);
              if(g_cart_id==0) fprintf(stdout, "# fuzzing prop. with Nlong=%d, N_APE=%d, alpha_APE=%f\n", Nlong, N_ape, alpha_ape);
              xchange_field_timeslice(g_spinor_field[i+hidx]);
              Fuzz_prop3(gauge_field_f, g_spinor_field[i+hidx], work, Nlong);
            }
          } else {
            /* smeared-local */
            ll = 2;
            chi = &g_spinor_field[2*n_s*n_c];
            psi = &g_spinor_field[hidx];
            for(i=0; i<n_s*n_c; i++) {
              sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, timeslice, i+n_s*n_c, mms2);
              read_lime_spinor(work, filename, 0);
              xchange_field(work);
              Qf5(g_spinor_field[i+hidx], work, muheavy);
            }
          }
        } else if(j==3) {
          ll = 3;
          if(Nlong>-1) {
            /* fuzzed-fuzzed -> phi[0-3]^dagger.phi[4-7] -> f.pf */
            chi = &g_spinor_field[n_s*n_c];
            psi = &g_spinor_field[hidx];
          } else {
            /* smeared-smeared -> phi[0-3]^dagger.phi[4-7] -> f.pf */
            chi = &g_spinor_field[3*n_s*n_c];
            psi = &g_spinor_field[hidx];
            for(i = 0; i < n_s*n_c; i++) {
              xchange_field_timeslice(g_spinor_field[i+hidx]);
              for(c=0; c<N_Jacobi; c++) {
                Jacobi_Smearing_Step_one(gauge_field_f, g_spinor_field[i+hidx], work, kappa_Jacobi);
                xchange_field_timeslice(g_spinor_field[i+hidx]);
              }
            }
          }
        }

        /************************************************************
         * the charged contractions
         ************************************************************/
        sl = 2*ll*T*K;
        itype = 1; 
        /* pion sector */
        for(idx=0; idx<9; idx++) {
          contract_twopoint(&cconn[sl], xgindex1[idx], xgindex2[idx], chi, psi, n_c);
/*          for(x0=0; x0<T; x0++) fprintf(stdout, "pion: %3d%25.16e%25.16e\n", x0, 
            cconn[sl+2*x0]/(double)VOL3/2./g_kappa/g_kappa, cconn[sl+2*x0+1]/(double)VOL3/2./g_kappa/g_kappa); */
          sl += (2*T);        
          itype++; 
        }

        /* rho sector */
        for(idx = 9; idx < 36; idx+=3) {
          for(i = 0; i < 3; i++) {
            for(x0=0; x0<2*T; x0++) Ctmp[x0] = 0.;
            contract_twopoint(Ctmp, xgindex1[idx+i], xgindex2[idx+i], chi, psi, n_c);
            for(x0=0; x0<T; x0++) {
              cconn[sl+2*x0  ] += (conf_gamma_sign[(idx-9)/3]*xvsign[idx-9+i]*Ctmp[2*x0  ]);
              cconn[sl+2*x0+1] += (conf_gamma_sign[(idx-9)/3]*xvsign[idx-9+i]*Ctmp[2*x0+1]);
            }
/*
            for(x0=0; x0<T; x0++) {
              x1 = (x0+timeslice)%T_global;
              fprintf(stdout, "rho: %3d%25.16e%25.16e\n", x0, 
                xvsign[idx-9+i]*Ctmp[2*x1  ]/(double)VOL3/2./g_kappa/g_kappa, 
                xvsign[idx-9+i]*Ctmp[2*x1+1]/(double)VOL3/2./g_kappa/g_kappa);
            }
*/
          }
          sl += (2*T); 
          itype++;
        }
    
        /* the a0 */
        contract_twopoint(&cconn[sl], xgindex1[36], xgindex2[36], chi, psi, n_c);
        sl += (2*T);
        itype++;

        /* the b1 */
        for(i=0; i<3; i++) {
          for(x0=0; x0<2*T; x0++) Ctmp[x0] = 0.;
          idx = 37;
          contract_twopoint(Ctmp, xgindex1[idx+i], xgindex2[idx+i], chi, psi, n_c);
          for(x0=0; x0<T; x0++) { 
            cconn[sl+2*x0  ] += (xvsign[idx-9+i]*Ctmp[2*x0  ]);
            cconn[sl+2*x0+1] += (xvsign[idx-9+i]*Ctmp[2*x0+1]);
          }
        }
      }  /* of j=0,...,3 */

#ifdef MPI
      /* collect the results */
#if (defined PARALLELTX) || (defined PARALLELTXY)
      MPI_Allreduce(cconn, buffer, 8*K*T, MPI_DOUBLE, MPI_SUM, g_ts_comm);
      MPI_Gather(buffer, 8*K*T, MPI_DOUBLE, cconn, 8*K*T, MPI_DOUBLE, 0, g_xs_comm);
#else
      MPI_Gather(cconn, 8*K*T, MPI_DOUBLE, buffer, 8*K*T, MPI_DOUBLE, 0, g_cart_grid);
      if(g_cart_id==0) memcpy((void*)cconn, (void*)buffer, 8*K*T_global*sizeof(double));
#endif
#endif
      /* write/add to file */
      if(g_cart_id==0) {
        sprintf(filename, "correl.%.4d.%.2d.%.2d.%.2d", Nconf, timeslice, mms1, k);
        if(count==0) {
          ofs=fopen(filename, "w");
        } else {
          ofs=fopen(filename, "a");
        }
        if( ofs == (FILE*)NULL ) {
          fprintf(stderr, "Error, could not open file %s for writing\n", filename);
#ifdef MPI
          MPI_Abort(MPI_COMM_WORLD, 10);
          MPI_Finalize();
#endif
          exit(8);
        }
        fprintf(stdout, "# writing correlators to file %s\n", filename);
        fprintf(ofs, "# %5d%3d%3d%3d%3d%15.8e%15.8e%15.8e%3d%3d\n", 
          Nconf, T_global, LX_global, LY_global, LZ, g_kappa, mms_masses[mms1], mms_masses[k], -sigmalight, -sigmaheavy);
        for(idx=0; idx<K; idx++) {
          for(ll=0; ll<4; ll++) {
            x1 = (0+timeslice) % T_global;
            i = 2* ( (x1/T)*4*K*T + ll*K*T + idx*T + x1%T ) + xisimag[idx];
            fprintf(ofs, "%3d%3d%4d%25.16e%25.16e\n", idx+1, 2*ll+1, 0,
              isneg[idx]*cconn[i]/(VOL3*g_nproc_x*g_nproc_y)/g_kappa/g_kappa/2., 0.);
            for(x0=1; x0<T_global/2; x0++) {
              x1 = ( x0+timeslice) % T_global;
              x2 = (-x0+timeslice+T_global) % T_global;
              i = 2* ( (x1/T)*4*K*T + ll*K*T + idx*T + x1%T ) + xisimag[idx];
              j = 2* ( (x2/T)*4*K*T + ll*K*T + idx*T + x2%T ) + xisimag[idx];
/*              fprintf(stdout, "idx=%d; x0=%d, x1=%d, x2=%d, i=%d, j=%d\n", idx, x0, x1, x2, i, j); */
              fprintf(ofs, "%3d%3d%4d%25.16e%25.16e\n", idx+1, 2*ll+1, x0,
                isneg[idx]*cconn[i]/(VOL3*g_nproc_x*g_nproc_y)/g_kappa/g_kappa/2., isneg[idx]*cconn[j]/(VOL3*g_nproc_x*g_nproc_y)/g_kappa/g_kappa/2.); 
            }
            x0 = T_global/2;
            x1 = (x0+timeslice) % T_global;
            i = 2* ( (x1/T)*4*K*T + ll*K*T + idx*T + x1%T ) + xisimag[idx];
            fprintf(ofs, "%3d%3d%4d%25.16e%25.16e\n", idx+1, 2*ll+1, x0,
              isneg[idx]*cconn[i]/(VOL3*g_nproc_x*g_nproc_y)/g_kappa/g_kappa/2., 0.);
          }
        }    
        fclose(ofs);
      }  /* of if g_cart_id == 0 */

    }  /* of i=0,...,g_no_extra_masses */
  }}  /* of sigmalight/sigmaheavy */

/* }   end of loop on mms1 */
#endif

  /**************************************************
   * free the allocated memory, finalize 
   **************************************************/
  free(g_gauge_field); 
  for(i=0; i<no_fields; i++) free(g_spinor_field[i]);
  free(g_spinor_field); 
  free_geometry(); 
  free(cconn);
  free(Ctmp);
  if(gauge_field_f != NULL) free(gauge_field_f);
  if(mms_masses!=NULL) free(mms_masses);
#ifdef MPI
  free(buffer); 
  MPI_Finalize();
#endif
  return(0);

}

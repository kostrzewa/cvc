/****************************************************
 * avc_conn_stochastic.c
 *
 * Thu Nov  5 11:01:58 CET 2009
 *
 * TODO:
 * DONE: 
 * CHANGES:
 ****************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef MPI
#  include <mpi.h>
#endif
#include "ifftw.h"
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

void usage() {
  fprintf(stdout, "Code to perform contractions for conn. contrib. to vac. pol.\n");
  fprintf(stdout, "Usage:    [options]\n");
  fprintf(stdout, "Options: -v verbose\n");
  fprintf(stdout, "         -g apply a random gauge transformation\n");
  fprintf(stdout, "         -f input filename [default cvc.input]\n");
#ifdef MPI
  MPI_Abort(MPI_COMM_WORLD, 1);
  MPI_Finalize();
#endif
  exit(0);
}


int main(int argc, char **argv) {
  
  int c, i, mu, nu;
  int count        = 0;
  int filename_set = 0;
  int dims[4]      = {0,0,0,0};
  int l_LX_at, l_LXstart_at;
  int x0, x1, x2, x3, ix, iix;
  int sid, sid2;
  double *conn1=NULL, *conn2=NULL;
  double *work = (double*)NULL;
  double q[4], fnorm;
  double unit_trace[2], shift_trace[2], D_trace[2];
  int verbose = 0;
  int do_gt   = 0;
  char filename[100], contype[400];
  double ratime, retime;
  double plaq;
  double spinor1[24], spinor2[24], U_[18];
  double contact_term[8], buffer[8];
  double *gauge_trafo=(double*)NULL;
  complex w, w1, *cp1, *cp2, *cp3;
  FILE *ofs;

  fftw_complex *in=(fftw_complex*)NULL;

#ifdef MPI
  fftwnd_mpi_plan plan_p, plan_m;
  int *status;
#else
  fftwnd_plan plan_p, plan_m;
#endif

#ifdef MPI
  MPI_Init(&argc, &argv);
#endif

  while ((c = getopt(argc, argv, "h?vgf:")) != -1) {
    switch (c) {
    case 'v':
      verbose = 1;
      break;
    case 'g':
      do_gt = 1;
      break;
    case 'f':
      strcpy(filename, optarg);
      filename_set=1;
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
  fprintf(stdout, "# Reading input from file %s\n", filename);
  read_input_parser(filename);

  /* some checks on the input data */
  if((T_global == 0) || (LX==0) || (LY==0) || (LZ==0)) {
    if(g_proc_id==0) fprintf(stdout, "T and L's must be set\n");
    usage();
  }
  if(g_kappa == 0.) {
    if(g_proc_id==0) fprintf(stdout, "kappa should be > 0.n");
    usage();
  }

  /* initialize MPI parameters */
  mpi_init(argc, argv);
#ifdef MPI
  if((status = (int*)calloc(g_nproc, sizeof(int))) == (int*)NULL) {
    MPI_Abort(MPI_COMM_WORLD, 1);
    MPI_Finalize();
    exit(7);
  }
#endif

  /* initialize fftw */
  dims[0]=T_global; dims[1]=LX; dims[2]=LY; dims[3]=LZ;
#ifdef MPI
  plan_p = fftwnd_mpi_create_plan(g_cart_grid, 4, dims, FFTW_BACKWARD, FFTW_MEASURE);
  plan_m = fftwnd_mpi_create_plan(g_cart_grid, 4, dims, FFTW_FORWARD, FFTW_MEASURE);
  fftwnd_mpi_local_sizes(plan_p, &T, &Tstart, &l_LX_at, &l_LXstart_at, &FFTW_LOC_VOLUME);
#else
  plan_p = fftwnd_create_plan(4, dims, FFTW_BACKWARD, FFTW_MEASURE | FFTW_IN_PLACE);
  plan_m = fftwnd_create_plan(4, dims, FFTW_FORWARD,  FFTW_MEASURE | FFTW_IN_PLACE);
  T            = T_global;
  Tstart       = 0;
  l_LX_at      = LX;
  l_LXstart_at = 0;
  FFTW_LOC_VOLUME = T*LX*LY*LZ;
#endif
  fprintf(stdout, "# [%2d] fftw parameters:\n"\
                  "# [%2d] T            = %3d\n"\
		  "# [%2d] Tstart       = %3d\n"\
		  "# [%2d] l_LX_at      = %3d\n"\
		  "# [%2d] l_LXstart_at = %3d\n"\
		  "# [%2d] FFTW_LOC_VOLUME = %3d\n", 
		  g_cart_id, g_cart_id, T, g_cart_id, Tstart, g_cart_id, l_LX_at,
		  g_cart_id, l_LXstart_at, g_cart_id, FFTW_LOC_VOLUME);

#ifdef MPI
  if(T==0) {
    fprintf(stderr, "[%2d] local T is zero; exit\n", g_cart_id);
    MPI_Abort(MPI_COMM_WORLD, 1);
    MPI_Finalize();
    exit(2);
  }
#endif

  if(init_geometry() != 0) {
    fprintf(stderr, "ERROR from init_geometry\n");
#ifdef MPI
    MPI_Abort(MPI_COMM_WORLD, 1);
    MPI_Finalize();
#endif
    exit(1);
  }

  geometry();


  /* read the gauge field */
  alloc_gauge_field(&g_gauge_field, VOLUMEPLUSRAND);
  if(strcmp(gaugefilename_prefix, "identity") == 0) {
    for(ix=0;ix<VOLUME;ix++) {
      for(mu=0;mu<4;mu++) {
        _cm_eq_id( g_gauge_field + _GGI(ix, mu) );
      }
    }
  } else {
    sprintf(filename, "%s.%.4d", gaugefilename_prefix, Nconf);
    if(g_cart_id==0) fprintf(stdout, "reading gauge field from file %s\n", filename);
    read_lime_gauge_field_doubleprec(filename);
  }
  xchange_gauge();

  /* measure the plaquette */
  plaquette(&plaq);
  if(g_cart_id==0) fprintf(stdout, "measured plaquette value: %25.16e\n", plaq);

  if(do_gt==1) {
    /***********************************
     * initialize gauge transformation
     ***********************************/
    init_gauge_trafo(&gauge_trafo, 1.);
    apply_gt_gauge(gauge_trafo);
    plaquette(&plaq);
    if(g_cart_id==0) fprintf(stdout, "measured plaquette value after gauge trafo: %25.16e\n", plaq);
  }

  /* allocate memory for the spinor fields */
  no_fields = 4;
  g_spinor_field = (double**)calloc(no_fields, sizeof(double*));
  for(i=0; i<no_fields; i++) alloc_spinor_field(&g_spinor_field[i], VOLUMEPLUSRAND);

  /* allocate memory for the contractions */
  conn1 = (double*)calloc(8*VOLUME, sizeof(double));
  conn2 = (double*)calloc(8*VOLUME, sizeof(double));
  if( conn1 == NULL || conn2 == NULL) { 
    fprintf(stderr, "could not allocate memory for conn\n");
#  ifdef MPI
    MPI_Abort(MPI_COMM_WORLD, 1);
    MPI_Finalize();
#  endif
    exit(3);
  }

  work  = (double*)calloc(32*VOLUME, sizeof(double));
  if( work == (double*)NULL ) { 
    fprintf(stderr, "could not allocate memory for work\n");
#  ifdef MPI
    MPI_Abort(MPI_COMM_WORLD, 1);
    MPI_Finalize();
#  endif
    exit(3);
  }

  for(ix=0; ix< 8*VOLUME; ix++) conn1[ix] = 0.;
  for(ix=0; ix< 8*VOLUME; ix++) conn2[ix] = 0.;
  for(ix=0; ix<32*VOLUME; ix++) work[ix]  = 0.;

  /***********************************************
   * prepare Fourier transformation arrays
   ***********************************************/
  in  = (fftw_complex*)malloc(FFTW_LOC_VOLUME*sizeof(fftw_complex));
  if(in==(fftw_complex*)NULL) {    
#ifdef MPI
    MPI_Abort(MPI_COMM_WORLD, 1);
    MPI_Finalize();
#endif
    exit(4);
  }

  /***************************
   * initialize contact_term
   ***************************/
  contact_term[0] = 0.;
  contact_term[1] = 0.;
  contact_term[2] = 0.;
  contact_term[3] = 0.;
  contact_term[4] = 0.;
  contact_term[5] = 0.;
  contact_term[6] = 0.;
  contact_term[7] = 0.;

  /***********************************************
   * start loop on source id.s 
   ***********************************************/
  for(sid=g_sourceid; sid<g_sourceid2; sid++) 
  // for(sid=g_sourceid; sid<=g_sourceid; sid++) 
  {

    /* read the new propagator for sid */
#ifdef MPI
    ratime = MPI_Wtime();
#else
    ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
    if(format==0) {
      sprintf(filename, "%s.%.4d.%.5d.inverted", filename_prefix, Nconf, sid);
      /* sprintf(filename, "%s.%.4d.%.2d", filename_prefix, Nconf, sid); */
      if(read_lime_spinor(g_spinor_field[1], filename, 0) != 0) break;
    }
    else if(format==1) {
      sprintf(filename, "%s.%.4d.%.5d.inverted", filename_prefix, Nconf, sid);
      if(read_cmi(g_spinor_field[1], filename) != 0) break;
    }
    xchange_field(g_spinor_field[1]);
#ifdef MPI
    retime = MPI_Wtime();
#else
    retime = (double)clock() / CLOCKS_PER_SEC;
#endif
    if(g_cart_id==0) fprintf(stdout, "time to read prop. for sid = %d: %e seconds\n", sid, retime-ratime);

    if(do_gt==1) {
      /******************************************
       * gauge transform the propagators for sid
       ******************************************/
      for(ix=0; ix<VOLUME; ix++) {
        _fv_eq_cm_ti_fv(spinor1, gauge_trafo+18*ix, g_spinor_field[1]+_GSI(ix));
        _fv_eq_fv(g_spinor_field[1]+_GSI(ix), spinor1);
      }
      xchange_field(g_spinor_field[1]);
    }

    /* calculate the source for sid: apply Q_phi_tbc */
#ifdef MPI
    ratime = MPI_Wtime();
#else
    ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
    Q_phi_tbc(g_spinor_field[0], g_spinor_field[1]);
    xchange_field(g_spinor_field[0]); 
#ifdef MPI
    retime = MPI_Wtime();
#else
    retime = (double)clock() / CLOCKS_PER_SEC;
#endif
    if(g_cart_id==0) fprintf(stdout, "time to calculate source for sid=%d: %e seconds\n", sid, retime-ratime);


  /******************************
   * second loop on source id
   ******************************/
  for(sid2=sid+1; sid2<=g_sourceid2; sid2++) {

    /* read the new propagator for sid2 */
#ifdef MPI
    ratime = MPI_Wtime();
#else
    ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
    if(format==0) {
      sprintf(filename, "%s.%.4d.%.5d.inverted", filename_prefix, Nconf, sid2);
      /* sprintf(filename, "%s.%.4d.%.2d", filename_prefix, Nconf, sid2); */
      if(read_lime_spinor(g_spinor_field[3], filename, 0) != 0) break;
    }
    else if(format==1) {
      sprintf(filename, "%s.%.4d.%.5d.inverted", filename_prefix, Nconf, sid2);
      if(read_cmi(g_spinor_field[3], filename) != 0) break;
    }
    xchange_field(g_spinor_field[3]);
#ifdef MPI
    retime = MPI_Wtime();
#else
    retime = (double)clock() / CLOCKS_PER_SEC;
#endif
    fprintf(stdout, "time to read prop. for sid2=%d: %e seconds\n", sid2, retime-ratime);

    if(do_gt==1) {
      /********************************************
       * gauge transform the propagators for sid2
       ********************************************/
      for(ix=0; ix<VOLUME; ix++) {
        _fv_eq_cm_ti_fv(spinor1, gauge_trafo+18*ix, g_spinor_field[3]+_GSI(ix));
        _fv_eq_fv(g_spinor_field[3]+_GSI(ix), spinor1);
      }
      xchange_field(g_spinor_field[3]);
    }

    /* calculate the source: apply Q_phi_tbc */
#ifdef MPI
    ratime = MPI_Wtime();
#else
    ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
    Q_phi_tbc(g_spinor_field[2], g_spinor_field[3]);
    xchange_field(g_spinor_field[2]); 
#ifdef MPI
    retime = MPI_Wtime();
#else
    retime = (double)clock() / CLOCKS_PER_SEC;
#endif
    if(g_cart_id==0) fprintf(stdout, "time to calculate source for sid2=%d: %e seconds\n", sid2, retime-ratime);

    count++;

    /* add new contractions to (existing) conn1/2 */
#  ifdef MPI
    ratime = MPI_Wtime();
#  else
    ratime = (double)clock() / CLOCKS_PER_SEC;
#  endif
    for(mu=0; mu<4; mu++) { /* loop on Lorentz index of the current */
      iix = _GWI(mu,0,VOLUME);
      for(ix=0; ix<VOLUME; ix++) {    /* loop on lattice sites */
        _cm_eq_cm_ti_co(U_, &g_gauge_field[_GGI(ix, mu)], &co_phase_up[mu]);

        /* first contribution */
        _fv_eq_cm_ti_fv(spinor1, U_, &g_spinor_field[1][_GSI(g_iup[ix][mu])]);
	_fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	_fv_mi_eq_fv(spinor2, spinor1);
	_co_eq_fv_dag_ti_fv(&w, &g_spinor_field[2][_GSI(ix)], spinor2);

        /* second contribution */
	_fv_eq_cm_dag_ti_fv(spinor1, U_, &g_spinor_field[1][_GSI(ix)]);
	_fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	_fv_pl_eq_fv(spinor2, spinor1);
	_co_eq_fv_dag_ti_fv(&w1, &g_spinor_field[2][_GSI(g_iup[ix][mu])], spinor2);

	conn1[iix  ] = 0.5 * ( w.re + w1.re );
	conn1[iix+1] = 0.5 * ( w.im + w1.im );

	iix += 2;
      }  /* of ix */
    }    /* of mu */

    for(mu=0; mu<4; mu++) { /* loop on Lorentz index of the current */
      iix = _GWI(mu,0,VOLUME);
      for(ix=0; ix<VOLUME; ix++) {    /* loop on lattice sites */
        _cm_eq_cm_ti_co(U_, &g_gauge_field[_GGI(ix, mu)], &co_phase_up[mu]);

        /* first contribution */
        _fv_eq_cm_ti_fv(spinor1, U_, &g_spinor_field[3][_GSI(g_iup[ix][mu])]);
	_fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	_fv_mi_eq_fv(spinor2, spinor1);
	_co_eq_fv_dag_ti_fv(&w, &g_spinor_field[0][_GSI(ix)], spinor2);

        /* second contribution */
	_fv_eq_cm_dag_ti_fv(spinor1, U_, &g_spinor_field[3][_GSI(ix)]);
	_fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	_fv_pl_eq_fv(spinor2, spinor1);
	_co_eq_fv_dag_ti_fv(&w1, &g_spinor_field[0][_GSI(g_iup[ix][mu])], spinor2);

	conn2[iix  ] = 0.5 * ( w.re + w1.re );
	conn2[iix+1] = 0.5 * ( w.im + w1.im );

	iix += 2;
      }  /* of ix */
    }    /* of mu */

#  ifdef MPI
    retime = MPI_Wtime();
#  else
    retime = (double)clock() / CLOCKS_PER_SEC;
#  endif
    fprintf(stdout, "[%2d] contractions for sid pair (%d,%d) in %e seconds\n", g_cart_id, sid, sid2, retime-ratime);

    /**************************************    
     * Fourier transform data, add to work
     **************************************/
#ifdef MPI
    ratime = MPI_Wtime();
#else
    ratime = (double)clock() / CLOCKS_PER_SEC;
#endif

    for(mu=0; mu<4; mu++) {
      memcpy((void*)in, (void*)(conn1+_GWI(mu,0,VOLUME)), 2*VOLUME*sizeof(double));
#ifdef MPI
      fftwnd_mpi(plan_p, 1, in, NULL, FFTW_NORMAL_ORDER);
#else
      fftwnd_one(plan_p, in, NULL);
#endif
      memcpy((void*)(conn1+_GWI(mu,0,VOLUME)), (void*)in, 2*VOLUME*sizeof(double));

      memcpy((void*)in, (void*)(conn2+_GWI(mu,0,VOLUME)), 2*VOLUME*sizeof(double));
#ifdef MPI
      fftwnd_mpi(plan_m, 1, in, NULL, FFTW_NORMAL_ORDER);
#else
      fftwnd_one(plan_m, in, NULL);
#endif
      memcpy((void*)(conn2+_GWI(mu,0,VOLUME)), (void*)in, 2*VOLUME*sizeof(double));
    }  /* of mu =0 ,..., 3*/

    /* add contrib. to work */
    for(mu=0; mu<4; mu++) {
    for(nu=0; nu<4; nu++) {
      for(ix=0; ix<VOLUME; ix++) {
        _co_eq_co_ti_co(&w, (complex*)(conn1+_GWI(mu,ix,VOLUME)), (complex*)(conn2+_GWI(nu,ix,VOLUME)));
        work[_GWI(4*mu+nu,ix,VOLUME)  ] += w.re;
        work[_GWI(4*mu+nu,ix,VOLUME)+1] += w.im;
      }
    }
    }

    if(g_cart_id==0) fprintf(stdout, "-------------------------------------------------------\n");

    if(sid == g_sourceid) {  // only for first value of sid
      /**********************************
       * add contrib. to contact term
       **********************************/
      for(mu=0; mu<4; mu++) {
        for(ix=0; ix<VOLUME; ix++) { 
          _cm_eq_cm_ti_co(U_, &g_gauge_field[_GGI(ix, mu)], &co_phase_up[mu]);
  
          // first contribution
          _fv_eq_cm_ti_fv(spinor1, U_, &g_spinor_field[3][_GSI(g_iup[ix][mu])]);
    	  _fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
  	  _fv_mi_eq_fv(spinor2, spinor1);
	  _co_eq_fv_dag_ti_fv(&w, &g_spinor_field[2][_GSI(ix)], spinor2);
	  contact_term[2*mu  ] -= 0.5 * w.re;
	  contact_term[2*mu+1] -= 0.5 * w.im;

          // second contribution
	  _fv_eq_cm_dag_ti_fv(spinor1, U_, &g_spinor_field[3][_GSI(ix)]);
	  _fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	  _fv_pl_eq_fv(spinor2, spinor1);
	  _co_eq_fv_dag_ti_fv(&w, &g_spinor_field[2][_GSI(g_iup[ix][mu])], spinor2);
	  contact_term[2*mu  ] += 0.5 * w.re;
	  contact_term[2*mu+1] += 0.5 * w.im;
        }
      }  // of mu
    }
  
  }  /* of sid2 */

    if( sid == g_sourceid ) {
      fprintf(stdout, "\n# [] normalizing contact term with count = %d\n", count);
      fnorm = 1. / ( (double)count * (double)(T_global*LX*LY*LZ) * g_prop_normsqr);
      fprintf(stdout, "\n# [] contact term fnorm = %25.16e\n", fnorm);
      contact_term[0] *= fnorm;
      contact_term[1] *= fnorm;
      contact_term[2] *= fnorm;
      contact_term[3] *= fnorm;
      contact_term[4] *= fnorm;
      contact_term[5] *= fnorm;
      contact_term[6] *= fnorm;
      contact_term[7] *= fnorm;
      fprintf(stdout, "\n# [] process %d contact term:\n", g_cart_id);
      for(mu=0; mu<4; mu++) fprintf(stdout, "\t %d\t%3d%25.16e%25.16e\n", g_cart_id, mu, contact_term[2*mu], contact_term[2*mu+1]);
    }

  }  /* of sid  */

  /***********************************
   * calculate final contact term
   ***********************************/
#ifdef MPI
  for(mu=0; mu<8; mu++) buffer[mu] = contact_term[mu];
  MPI_Allreduce(buffer, contact_term, 8, MPI_DOUBLE, MPI_SUM, g_cart_grid);
#endif
  if(g_cart_id==0) {
    fprintf(stdout, "\n# [] final contact term:\n");
    for(mu=0; mu<4; mu++) fprintf(stdout, "%3d%25.16e%25.16e\n", mu, contact_term[2*mu], contact_term[2*mu+1]);
  }

  /*********************************************************
   * add phase factor, normalize and subtract contact term
   * - note minus sign in fnorm from fermion loop
   *********************************************************/
  fprintf(stdout, "\n# [] normalizing conn with count = %d\n", count);
  fnorm = -1. / ( (double)(T_global*LX*LY*LZ) * (double)(count) * g_prop_normsqr * g_prop_normsqr);
  fprintf(stdout, "\n# [] conn fnorm = %e\n", fnorm);
  for(mu=0; mu<4; mu++) {
  for(nu=0; nu<4; nu++) {
     
    for(x0=0; x0<T; x0++) {
      q[0] = (double)(x0+Tstart) / (double)T_global;
    for(x1=0; x1<LX; x1++) {
      q[1] = (double)(x1) / (double)LX;
    for(x2=0; x2<LY; x2++) {
      q[2] = (double)(x2) / (double)LY;
    for(x3=0; x3<LZ; x3++) {
      q[3] = (double)(x3) / (double)LZ;
      ix = g_ipt[x0][x1][x2][x3];
      w.re = cos( M_PI * (q[mu]-q[nu]) );
      w.im = sin( M_PI * (q[mu]-q[nu]) );
      _co_eq_co_ti_co(&w1, (complex*)(work+_GWI(4*mu+nu,ix,VOLUME)), &w);
      _co_eq_co_ti_re((complex*)(work+_GWI(4*mu+nu,ix,VOLUME)), &w1, fnorm);
      if(mu == nu) {
        work[_GWI(5*mu,ix,VOLUME)  ] -= contact_term[2*mu  ];
        work[_GWI(5*mu,ix,VOLUME)+1] -= contact_term[2*mu+1];
      }
    }}}} 
  }}
  
  /*****************************************
   * save the result in momentum space
   *****************************************/
  sprintf(filename, "cvc_conn_P.%.4d.%.4d_%.4d", Nconf, g_sourceid, g_sourceid2);
  sprintf(contype, "cvc - cvc in momentum space, all 16 components, stochastic volume propagators");
  // write_contraction(work, NULL, filename, 16, 0, 0);
  write_lime_contraction(work, filename, 64, 16, contype, Nconf, 0);



  sprintf(filename, "cvc_conn_P.%.4d.%.4d_%.4d.ascii", Nconf, g_sourceid, g_sourceid2);
  write_contraction(work, NULL, filename, 16, 2, 0);

  /*****************************************
   * free the allocated memory, finalize
   *****************************************/
  free(g_gauge_field);
  for(i=0; i<no_fields; i++) free(g_spinor_field[i]);
  free(g_spinor_field);
  free_geometry();
  fftw_free(in);
  free(conn1);
  free(conn2);
  free(work);
  if(do_gt==1) free(gauge_trafo);

#ifdef MPI
  fftwnd_mpi_destroy_plan(plan_p);
  fftwnd_mpi_destroy_plan(plan_m);
  free(status);
  MPI_Finalize();
#else
  fftwnd_destroy_plan(plan_p);
  fftwnd_destroy_plan(plan_m);
#endif

  return(0);

}

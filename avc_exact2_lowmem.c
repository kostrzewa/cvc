/****************************************************
  
 * avc_exact2_lowmem.c
 *
 * Fri Apr 29 14:08:27 CEST 2011
 *
 * PURPOSE:
 * - like avc_exact2 but with less memory demand (to run safely on jugene)
 * DONE:
 * TODO:
 ****************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef MPI
#  include <mpi.h>
#endif
#ifdef OPENMP
#  include <omp.h>
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
#include "contractions_io.h"
#include "Q_phi.h"
#include "read_input_parser.h"

void usage() {
  fprintf(stdout, "Code to perform AV current correlator conn. contractions\n");
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
  
  int c, i, j, mu, nu, ir, is, ia, ib, imunu;
  int filename_set = 0;
  int dims[4]      = {0,0,0,0};
  int l_LX_at, l_LXstart_at;
  int source_location, have_source_flag = 0;
  int x0, x1, x2, x3, ix;
  int sx0, sx1, sx2, sx3;
  int isimag[4];
  int gperm[5][4], gperm2[4][4];
  int check_position_space_WI=0, check_momentum_space_WI=0;
  int num_threads = 1, nthreads=-1, threadid=-1;
  int exitstatus;
  int write_ascii=0;
  int mms = 0, mass_id = -1;
  int outfile_prefix_set = 0;
  int up_dn_onefile = 0;
  double gperm_sign[5][4], gperm2_sign[4][4];
  double *conn = (double*)NULL;
  double contact_term[8];
  double phase[4];
  double *work=NULL;
  int verbose = 0;
  int do_gt   = 0;
  char filename[100], contype[400], outfile_prefix[400];
  double ratime, retime;
  double plaq;
  double spinor1[24], spinor2[24], U_[18];
  double *gauge_trafo=(double*)NULL;
  double *phi=NULL, *chi=NULL;
  complex w, w1;
  double Usourcebuff[72], *Usource[4];
  FILE *ofs;
  int use_shifted_spinor = 0, shift_vector[4];

  fftw_complex *in=(fftw_complex*)NULL;

#ifdef MPI
  fftwnd_mpi_plan plan_p;
  int *status;
#else
  fftwnd_plan plan_p;
#endif

#ifdef MPI
  MPI_Init(&argc, &argv);
#endif

  while ((c = getopt(argc, argv, "ucwWah?vgf:t:m:o:")) != -1) {
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
    case 'w':
      check_position_space_WI = 1;
      fprintf(stdout, "\n# [avc_exact2_lowmem] will check Ward identity in position space\n");
      break;
    case 'W':
      check_momentum_space_WI = 1;
      fprintf(stdout, "\n# [avc_exact2_lowmem] will check Ward identity in momentum space\n");
      break;
    case 't':
      num_threads = atoi(optarg);
      fprintf(stdout, "\n# [avc_exact2_lowmem] will use %d threads in spacetime loops\n", num_threads);
      break;
    case 'a':
      write_ascii = 1;
      fprintf(stdout, "\n# [avc_exact2_lowmem] will write data in ASCII format too\n");
      break;
    case 'm':
      mms = 1;
      mass_id = atoi(optarg);
      fprintf(stdout, "\n# [avc_exact2_lowmem] will read propagators in MMS format with mass id $d\n", mass_id);
      break;
    case 'o':
      strcpy(outfile_prefix, optarg);
      fprintf(stdout, "\n# [avc_exact2_lowmem] will use prefix $s for output filenames\n", outfile_prefix);
      outfile_prefix_set = 1;
      break;
    case 'c':
      use_shifted_spinor = 1;
      fprintf(stdout, "# [avc_exact2_lowmem] will use shifted free spinor fields\n");
      break;
    case 'u':
      up_dn_onefile = 1;
      fprintf(stdout, "# [avc_exact2_lowmem] will read up / dn from one file at pos 0 / 1\n");
      break;
    case 'h':
    case '?':
    default:
      usage();
      break;
    }
  }

  if(g_cart_id==0) {
    g_the_time = time(NULL);
    fprintf(stdout, "\n# [avc_exact2_lowmem] using global time stamp %s", ctime(&g_the_time));
  }

  /*********************************
   * set number of openmp threads
   *********************************/
#ifdef OPENMP
  omp_set_num_threads(num_threads);
#endif

  /* set the default values */
  if(filename_set==0) strcpy(filename, "cvc.input");
  fprintf(stdout, "# Reading input from file %s\n", filename);
  read_input_parser(filename);

  /* some checks on the input data */
  if((T_global == 0) || (LX==0) || (LY==0) || (LZ==0)) {
    if(g_proc_id==0) fprintf(stderr, "\n[avc_exact2_lowmem] T and L's must be set\n");
    usage();
  }
  if(g_kappa == 0.) {
    if(g_proc_id==0) fprintf(stderr, "\n[avc_exact2_lowmem] kappa should be > 0.n");
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

  /*******************************
   * initialize fftw
   *******************************/
#ifdef OPENMP
  exitstatus = fftw_threads_init();
  if(exitstatus != 0) {
    fprintf(stderr, "\n[avc_exact2_lowmem] Error from fftw_init_threads; status was %d\n", exitstatus);
    exit(120);
  }
#endif


  dims[0]=T_global; dims[1]=LX; dims[2]=LY; dims[3]=LZ;
#ifdef MPI
  plan_p = fftwnd_mpi_create_plan(g_cart_grid, 4, dims, FFTW_BACKWARD, FFTW_MEASURE);
  fftwnd_mpi_local_sizes(plan_p, &T, &Tstart, &l_LX_at, &l_LXstart_at, &FFTW_LOC_VOLUME);
#else
  plan_p = fftwnd_create_plan(4, dims, FFTW_BACKWARD, FFTW_MEASURE | FFTW_IN_PLACE);
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

  alloc_gauge_field(&g_gauge_field, VOLUMEPLUSRAND);
  if(!(strcmp(gaugefilename_prefix,"identity")==0)) {
    /* read the gauge field */
    sprintf(filename, "%s.%.4d", gaugefilename_prefix, Nconf);
    if(g_cart_id==0) fprintf(stdout, "reading gauge field from file %s\n", filename);
    read_lime_gauge_field_doubleprec(filename);
  } else {
    /* initialize unit matrices */
    if(g_cart_id==0) fprintf(stdout, "\n# [avc_exact] initializing unit matrices\n");
    for(ix=0;ix<VOLUME;ix++) {
      _cm_eq_id( g_gauge_field + _GGI(ix, 0) );
      _cm_eq_id( g_gauge_field + _GGI(ix, 1) );
      _cm_eq_id( g_gauge_field + _GGI(ix, 2) );
      _cm_eq_id( g_gauge_field + _GGI(ix, 3) );
    }
  }
#ifdef MPI
  xchange_gauge();
#endif

  /* measure the plaquette */
  plaquette(&plaq);
  if(g_cart_id==0) fprintf(stdout, "measured plaquette value: %25.16e\n", plaq);

  /* allocate memory for the spinor fields */
  no_fields = 6;
  if(mms || use_shifted_spinor) no_fields++;
  g_spinor_field = (double**)calloc(no_fields, sizeof(double*));
  for(i=0; i<no_fields; i++) alloc_spinor_field(&g_spinor_field[i], VOLUMEPLUSRAND);
  if(mms || use_shifted_spinor) {
    work = g_spinor_field[no_fields-1];
  }

  /* allocate memory for the contractions */
  conn = (double*)calloc(2 * 16 * VOLUME, sizeof(double));
  if( conn==(double*)NULL ) {
    fprintf(stderr, "could not allocate memory for contr. fields\n");
#ifdef MPI
    MPI_Abort(MPI_COMM_WORLD, 1);
    MPI_Finalize();
#endif
    exit(3);
  }
#ifdef OPENMP
#pragma omp parallel for
#endif
  for(ix=0; ix<32*VOLUME; ix++) conn[ix] = 0.;

  /***********************************************************
   * prepare Fourier transformation arrays
   ***********************************************************/
  in  = (fftw_complex*)malloc(FFTW_LOC_VOLUME*sizeof(fftw_complex));
  if(in==(fftw_complex*)NULL) {    
#ifdef MPI
    MPI_Abort(MPI_COMM_WORLD, 1);
    MPI_Finalize();
#endif
    exit(4);
  }

  /***********************************************************
   * determine source coordinates, find out, if source_location is in this process
   ***********************************************************/
  have_source_flag = (int)(g_source_location/(LX*LY*LZ)>=Tstart && g_source_location/(LX*LY*LZ)<(Tstart+T));
  if(have_source_flag==1) fprintf(stdout, "process %2d has source location\n", g_cart_id);
  sx0 = g_source_location/(LX*LY*LZ)-Tstart;
  sx1 = (g_source_location%(LX*LY*LZ)) / (LY*LZ);
  sx2 = (g_source_location%(LY*LZ)) / LZ;
  sx3 = (g_source_location%LZ);
  Usource[0] = Usourcebuff;
  Usource[1] = Usourcebuff+18;
  Usource[2] = Usourcebuff+36;
  Usource[3] = Usourcebuff+54;
  if(have_source_flag==1) { 
    fprintf(stdout, "local source coordinates: (%3d,%3d,%3d,%3d)\n", sx0, sx1, sx2, sx3);
    source_location = g_ipt[sx0][sx1][sx2][sx3];
    _cm_eq_cm_ti_co(Usource[0], &g_gauge_field[_GGI(source_location,0)], &co_phase_up[0]);
    _cm_eq_cm_ti_co(Usource[1], &g_gauge_field[_GGI(source_location,1)], &co_phase_up[1]);
    _cm_eq_cm_ti_co(Usource[2], &g_gauge_field[_GGI(source_location,2)], &co_phase_up[2]);
    _cm_eq_cm_ti_co(Usource[3], &g_gauge_field[_GGI(source_location,3)], &co_phase_up[3]);
  }
#ifdef MPI
  MPI_Gather(&have_source_flag, 1, MPI_INT, status, 1, MPI_INT, 0, g_cart_grid);
  if(g_cart_id==0) {
    for(mu=0; mu<g_nproc; mu++) fprintf(stdout, "status[%1d]=%d\n", mu,status[mu]);
  }
  if(g_cart_id==0) {
    for(have_source_flag=0; status[have_source_flag]!=1; have_source_flag++);
    fprintf(stdout, "have_source_flag= %d\n", have_source_flag);
  }
  MPI_Bcast(&have_source_flag, 1, MPI_INT, 0, g_cart_grid);
  MPI_Bcast(Usourcebuff, 72, MPI_DOUBLE, have_source_flag, g_cart_grid);
  fprintf(stdout, "[%2d] have_source_flag = %d\n", g_cart_id, have_source_flag);
#else
  have_source_flag = 0;
#endif

#ifdef MPI
      ratime = MPI_Wtime();
#else
      ratime = (double)clock() / CLOCKS_PER_SEC;
#endif

  /***********************************************************
   *  initialize the Gamma matrices
   ***********************************************************/
  // gamma_5:
  gperm[4][0] = gamma_permutation[5][ 0] / 6;
  gperm[4][1] = gamma_permutation[5][ 6] / 6;
  gperm[4][2] = gamma_permutation[5][12] / 6;
  gperm[4][3] = gamma_permutation[5][18] / 6;
  gperm_sign[4][0] = gamma_sign[5][ 0];
  gperm_sign[4][1] = gamma_sign[5][ 6];
  gperm_sign[4][2] = gamma_sign[5][12];
  gperm_sign[4][3] = gamma_sign[5][18];
  // gamma_nu gamma_5
  for(nu=0;nu<4;nu++) {
    // permutation
    gperm[nu][0] = gamma_permutation[6+nu][ 0] / 6;
    gperm[nu][1] = gamma_permutation[6+nu][ 6] / 6;
    gperm[nu][2] = gamma_permutation[6+nu][12] / 6;
    gperm[nu][3] = gamma_permutation[6+nu][18] / 6;
    // is imaginary ?
    isimag[nu] = gamma_permutation[6+nu][0] % 2;
    // (overall) sign
    gperm_sign[nu][0] = gamma_sign[6+nu][ 0];
    gperm_sign[nu][1] = gamma_sign[6+nu][ 6];
    gperm_sign[nu][2] = gamma_sign[6+nu][12];
    gperm_sign[nu][3] = gamma_sign[6+nu][18];
    // write to stdout
    fprintf(stdout, "# gamma_%d5 = (%f %d, %f %d, %f %d, %f %d)\n", nu,
        gperm_sign[nu][0], gperm[nu][0], gperm_sign[nu][1], gperm[nu][1], 
        gperm_sign[nu][2], gperm[nu][2], gperm_sign[nu][3], gperm[nu][3]);
  }
  // gamma_nu
  for(nu=0;nu<4;nu++) {
    // permutation
    gperm2[nu][0] = gamma_permutation[nu][ 0] / 6;
    gperm2[nu][1] = gamma_permutation[nu][ 6] / 6;
    gperm2[nu][2] = gamma_permutation[nu][12] / 6;
    gperm2[nu][3] = gamma_permutation[nu][18] / 6;
    // (overall) sign
    gperm2_sign[nu][0] = gamma_sign[nu][ 0];
    gperm2_sign[nu][1] = gamma_sign[nu][ 6];
    gperm2_sign[nu][2] = gamma_sign[nu][12];
    gperm2_sign[nu][3] = gamma_sign[nu][18];
    // write to stdout
    fprintf(stdout, "# gamma_%d = (%f %d, %f %d, %f %d, %f %d)\n", nu,
        gperm2_sign[nu][0], gperm2[nu][0], gperm2_sign[nu][1], gperm2[nu][1], 
        gperm2_sign[nu][2], gperm2[nu][2], gperm2_sign[nu][3], gperm2[nu][3]);
  }

  // initialize the contact term
  for(i=0;i<8;i++) contact_term[i] = 0.;


  /**********************************************************
   **********************************************************
   **
   ** first contribution
   **
   **********************************************************
   **********************************************************/  

  /**********************************************
   * loop on the Lorentz index nu at source 
   **********************************************/
  for(nu=0; nu<4; nu++) 
  //for(nu=0; nu<4; nu++) 
  {
    // fprintf(stdout, "\n# [avc_exact2_lowmem] 1st part, processing nu = %d ...\n", nu);

    memset(shift_vector, 0, 4*sizeof(int));
    shift_vector[nu] = 1;
    // TEST
    fprintf(stdout, "# [] shift_vector(%d) = (%d, %d, %d, %d)\n", nu,
    shift_vector[0], shift_vector[1], shift_vector[2], shift_vector[3]);
               
    for(ir=0; ir<4; ir++) {

      // read 3 up-type propagator color components for spinor index ir
      for(ia=0; ia<3; ia++) {
	if(!mms) {
      	  get_filename(filename, 4, 3*ir+ia, 1);
          exitstatus = read_lime_spinor(g_spinor_field[ia], filename, 0);
          if(exitstatus != 0) {
            fprintf(stderr, "\n# [avc_exact2_lowmem] Error from read_lime_spinor\n");
            exit(111);
          }
          xchange_field(g_spinor_field[ia]);
        } else {
          sprintf(filename, "%s.%.4d.04.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, 3*ir+ia, mass_id);
          exitstatus = read_lime_spinor(work, filename, 0);
          if(exitstatus != 0) {
            fprintf(stderr, "\n# [avc_exact2_lowmem] Error from read_lime_spinor\n");
            exit(111);
          }
          xchange_field(work);
          Qf5(g_spinor_field[ia], work, -g_mu);
          xchange_field(g_spinor_field[ia]);
        }

      }  // of ia

      // read 3 dn-type propagator color components for spinor index gamma_perm ( ir )
      for(ia=0; ia<3; ia++) {
        if(!mms) {
          if(!up_dn_onefile) {
            if(!use_shifted_spinor) {
              get_filename(filename, nu, 3*gperm[nu][ir]+ia, -1);
              exitstatus = read_lime_spinor(g_spinor_field[3+ia], filename, 0);
            } else {
              get_filename(filename, 4, 3*gperm[nu][ir]+ia, -1);
              exitstatus = read_lime_spinor(work, filename, 0);
              shift_spinor_field(g_spinor_field[3+ia], work, shift_vector);
            }
          } else {
            if(!use_shifted_spinor) {
              get_filename(filename, nu, 3*gperm[nu][ir]+ia, 1);
              exitstatus = read_lime_spinor(g_spinor_field[3+ia], filename, 1);
            } else {
              get_filename(filename, 4, 3*gperm[nu][ir]+ia, 1);
              exitstatus = read_lime_spinor(work, filename, 1);
              shift_spinor_field(g_spinor_field[3+ia], work, shift_vector);
            }
          }
          if(exitstatus != 0) {
            fprintf(stderr, "\n# [avc_exact2_lowmem] Error from read_lime_spinor\n");
            exit(111);
          }
          xchange_field(g_spinor_field[3+ia]);
        } else {
          sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, nu, 3*gperm[nu][ir]+ia, mass_id);
          exitstatus = read_lime_spinor(work, filename, 0);
          if(exitstatus != 0) {
            fprintf(stderr, "\n# [avc_exact2_lowmem] Error from read_lime_spinor\n");
            exit(111);
          }
          xchange_field(work);
          Qf5(g_spinor_field[3+ia], work, g_mu);
          xchange_field(g_spinor_field[3+ia]);
        }
      }  // of ia

      for(ia=0; ia<3; ia++) {
        phi = g_spinor_field[ia];
      for(ib=0; ib<3; ib++) {
        chi = g_spinor_field[3+ib];
        //fprintf(stdout, "\n# [nu5] spin index pair (%d, %d); col index pair (%d, %d)\n", ir, gperm[nu][ir], ia ,ib);
        // 1) gamma_nu gamma_5 x U
        for(mu=0; mu<4; mu++) 
        //for(mu=0; mu<1; mu++) 
        {

          imunu = 4*mu+nu;
#ifdef OPENMP
#pragma omp parallel for private(ix, spinor1, spinor2, U_, w, w1)  shared(imunu, ia, ib, nu, mu)
#endif
          for(ix=0; ix<VOLUME; ix++) {
/*
            threadid = omp_get_thread_num();
            nthreads = omp_get_num_threads();
            fprintf(stdout, "[thread%d] number of threads = %d\n", threadid, nthreads);
*/

            _cm_eq_cm_ti_co(U_, &g_gauge_field[_GGI(ix,mu)], &co_phase_up[mu]);

            _fv_eq_cm_ti_fv(spinor1, U_, phi+_GSI(g_iup[ix][mu]));
            _fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	    _fv_mi_eq_fv(spinor2, spinor1);
	    _fv_eq_gamma_ti_fv(spinor1, 5, spinor2);
	    _co_eq_fv_dag_ti_fv(&w, chi+_GSI(ix), spinor1);
            _co_eq_co_ti_co(&w1, &w, (complex*)(Usource[nu]+2*(3*ia+ib)));
            if(!isimag[nu]) {
              conn[_GWI(imunu,ix,VOLUME)  ] += gperm_sign[nu][ir] * w1.re;
              conn[_GWI(imunu,ix,VOLUME)+1] += gperm_sign[nu][ir] * w1.im;
            } else {
              conn[_GWI(imunu,ix,VOLUME)  ] += gperm_sign[nu][ir] * w1.im;
              conn[_GWI(imunu,ix,VOLUME)+1] -= gperm_sign[nu][ir] * w1.re;
            }
            //if(ix==0) fprintf(stdout, "[1_nu5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib, w.re, w.im);
            //if(ix==0) fprintf(stdout, "[1_nu5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib,
            //    gperm_sign[nu][ir] * w1.re, gperm_sign[nu][ir] * w1.im);

          }  // of ix

#ifdef OPENMP
#pragma omp parallel for private(ix, spinor1, spinor2, U_, w, w1)  shared(imunu, ia, ib, nu, mu)
#endif
          for(ix=0; ix<VOLUME; ix++) {
            _cm_eq_cm_ti_co(U_, &g_gauge_field[_GGI(ix,mu)], &co_phase_up[mu]);

            _fv_eq_cm_dag_ti_fv(spinor1, U_, phi+_GSI(ix));
            _fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	    _fv_pl_eq_fv(spinor2, spinor1);
	    _fv_eq_gamma_ti_fv(spinor1, 5, spinor2);
	    _co_eq_fv_dag_ti_fv(&w, chi+_GSI(g_iup[ix][mu]), spinor1);
            _co_eq_co_ti_co(&w1, &w, (complex*)(Usource[nu]+2*(3*ia+ib)));
            if(!isimag[nu]) {
              conn[_GWI(imunu,ix,VOLUME)  ] += gperm_sign[nu][ir] * w1.re;
              conn[_GWI(imunu,ix,VOLUME)+1] += gperm_sign[nu][ir] * w1.im;
            } else {
              conn[_GWI(imunu,ix,VOLUME)  ] += gperm_sign[nu][ir] * w1.im;
              conn[_GWI(imunu,ix,VOLUME)+1] -= gperm_sign[nu][ir] * w1.re;
            }
            //if(ix==0) fprintf(stdout, "[3_nu5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib, w.re, w.im);
            //if(ix==0) fprintf(stdout, "[3_nu5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib,
            //    gperm_sign[nu][ir] * w1.re, gperm_sign[nu][ir] * w1.im);

          }  // of ix
	} // of mu
      } /* of ib */
      }  // of ia

      // read 3 dn-type propagator color components for spinor index id( ir )
      for(ib=0; ib<3; ib++) {
        if(!mms) {
          if(!up_dn_onefile) {
            if(!use_shifted_spinor) {
              get_filename(filename, nu, 3*gperm[4][ir]+ib, -1);
              exitstatus = read_lime_spinor(g_spinor_field[3+ib], filename, 0);
            } else {
              get_filename(filename, 4, 3*gperm[4][ir]+ib, -1);
              exitstatus = read_lime_spinor(work, filename, 0);
              shift_spinor_field(g_spinor_field[3+ib], work, shift_vector);
            }
          } else {
            if(!use_shifted_spinor) {
              get_filename(filename, nu, 3*gperm[4][ir]+ib, 1);
              exitstatus = read_lime_spinor(g_spinor_field[3+ib], filename, 1);
            } else {
              get_filename(filename, 4, 3*gperm[4][ir]+ib, 1);
              exitstatus = read_lime_spinor(work, filename, 1);
              shift_spinor_field(g_spinor_field[3+ib], work, shift_vector);
            }
          }
          if(exitstatus != 0) {
            fprintf(stderr, "\n# [avc_exact2_lowmem] Error from read_lime_spinor\n");
            exit(111);
          }
          xchange_field(g_spinor_field[3+ib]);
        } else {
          sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, nu, 3*gperm[4][ir]+ib, mass_id);
          exitstatus = read_lime_spinor(work, filename, 0);
          if(exitstatus != 0) {
            fprintf(stderr, "\n# [avc_exact2_lowmem] Error from read_lime_spinor\n");
            exit(111);
          }
          xchange_field(work);
          Qf5(g_spinor_field[3+ib], work, g_mu);
          xchange_field(g_spinor_field[3+ib]);
        }
      }  // of ib

      for(ia=0; ia<3; ia++) {
        phi = g_spinor_field[ia];
      for(ib=0; ib<3; ib++) {
        chi = g_spinor_field[3+ib];
        //fprintf(stdout, "\n# [5] spin index pair (%d, %d); col index pair (%d, %d)\n", ir, gperm[4][ir], ia ,ib);

        // -gamma_5 x U
        for(mu=0; mu<4; mu++) 
        //for(mu=0; mu<1; mu++) 
        {

          imunu = 4*mu+nu;

#ifdef OPENMP
#pragma omp parallel for private(ix, spinor1, spinor2, U_, w, w1)  shared(imunu, ia, ib, nu, mu)
#endif
          for(ix=0; ix<VOLUME; ix++) {
            _cm_eq_cm_ti_co(U_, &g_gauge_field[_GGI(ix,mu)], &co_phase_up[mu]);

            _fv_eq_cm_ti_fv(spinor1, U_, phi+_GSI(g_iup[ix][mu]));
            _fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	    _fv_mi_eq_fv(spinor2, spinor1);
	    _fv_eq_gamma_ti_fv(spinor1, 5, spinor2);
	    _co_eq_fv_dag_ti_fv(&w, chi+_GSI(ix), spinor1);
            _co_eq_co_ti_co(&w1, &w, (complex*)(Usource[nu]+2*(3*ia+ib)));
            conn[_GWI(imunu,ix,VOLUME)  ] -= gperm_sign[4][ir] * w1.re;
            conn[_GWI(imunu,ix,VOLUME)+1] -= gperm_sign[4][ir] * w1.im;
            //if(ix==0) fprintf(stdout, "[1_5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib, w.re, w.im);
            //if(ix==0) fprintf(stdout, "[1_5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib,
            //    gperm_sign[4][ir] * w1.re, gperm_sign[4][ir] * w1.im);

          }  // of ix

#ifdef OPENMP
#pragma omp parallel for private(ix, spinor1, spinor2, U_, w, w1)  shared(imunu, ia, ib, nu, mu)
#endif
          for(ix=0; ix<VOLUME; ix++) {
            _cm_eq_cm_ti_co(U_, &g_gauge_field[_GGI(ix,mu)], &co_phase_up[mu]);

            _fv_eq_cm_dag_ti_fv(spinor1, U_, phi+_GSI(ix));
            _fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	    _fv_pl_eq_fv(spinor2, spinor1);
	    _fv_eq_gamma_ti_fv(spinor1, 5, spinor2);
	    _co_eq_fv_dag_ti_fv(&w, chi+_GSI(g_iup[ix][mu]), spinor1);
            _co_eq_co_ti_co(&w1, &w, (complex*)(Usource[nu]+2*(3*ia+ib)));
            conn[_GWI(imunu,ix,VOLUME)  ] -= gperm_sign[4][ir] * w1.re;
            conn[_GWI(imunu,ix,VOLUME)+1] -= gperm_sign[4][ir] * w1.im;
            //if(ix==0) fprintf(stdout, "[3_5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib, w.re, w.im);
            //if(ix==0) fprintf(stdout, "[3_5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib,
            //    gperm_sign[4][ir] * w1.re, gperm_sign[4][ir] * w1.im);

          }  // of ix
	}  // of mu
      }  // of ib

      // contribution to contact term
      if(have_source_flag == g_cart_id) {
        _fv_eq_cm_ti_fv(spinor1, Usource[nu], phi+_GSI(g_iup[source_location][nu]));
        _fv_eq_gamma_ti_fv(spinor2, nu, spinor1);
        _fv_mi_eq_fv(spinor2, spinor1);
        contact_term[2*nu  ] += -0.5 * spinor2[2*(3*ir+ia)  ];
        contact_term[2*nu+1] += -0.5 * spinor2[2*(3*ir+ia)+1];
      }
      }  // of ia
    }  // of ir

  }  // of nu

  if(have_source_flag == g_cart_id) {
    fprintf(stdout, "\n# [avc_exact2_lowmem] contact term after 1st part:\n");
    fprintf(stdout, "\t%d\t%25.16e%25.16e\n", 0, contact_term[0], contact_term[1]);
    fprintf(stdout, "\t%d\t%25.16e%25.16e\n", 1, contact_term[2], contact_term[3]);
    fprintf(stdout, "\t%d\t%25.16e%25.16e\n", 2, contact_term[4], contact_term[5]);
    fprintf(stdout, "\t%d\t%25.16e%25.16e\n", 3, contact_term[6], contact_term[7]);
  }

  /**********************************************************
   **********************************************************
   **
   ** second contribution
   **
   **********************************************************
   **********************************************************/  

  /**********************************************
   * loop on the Lorentz index nu at source 
   **********************************************/
  for(nu=0; nu<4; nu++) 
  //for(nu=0; nu<4; nu++) 
  {

    // fprintf(stdout, "\n# [avc_exact2_lowmem] 2nd part, processing nu = %d ...\n", nu);

    memset(shift_vector, 0, 4*sizeof(int));
    shift_vector[nu] = +1;
    // TEST
    fprintf(stdout, "# [] shift_vector(%d) = (%d, %d, %d, %d)\n", nu,
    shift_vector[0], shift_vector[1], shift_vector[2], shift_vector[3]);
                
    for(ir=0; ir<4; ir++) {

      // 3 dn-type propagators for spinor index ir    
      for(ia=0; ia<3; ia++) {
        if(!mms) {
          if(!up_dn_onefile) {
            get_filename(filename, 4, 3*gperm[nu][ir]+ia, -1);
            exitstatus = read_lime_spinor(g_spinor_field[3+ia], filename, 0);
          } else {
            get_filename(filename, 4, 3*gperm[nu][ir]+ia, 1);
            exitstatus = read_lime_spinor(g_spinor_field[3+ia], filename, 1);
          }
         if(exitstatus != 0) {
            fprintf(stderr, "\n# [avc_exact2_lowmem] Error from read_lime_spinor\n");
            exit(111);
          }
          xchange_field(g_spinor_field[3+ia]);
        } else {
          sprintf(filename, "%s.%.4d.04.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, 3*gperm[nu][ir]+ia, mass_id);
          exitstatus = read_lime_spinor(work, filename, 0);
          if(exitstatus != 0) {
            fprintf(stderr, "\n# [avc_exact2_lowmem] Error from read_lime_spinor\n");
            exit(111);
          }
          xchange_field(work);
          Qf5(g_spinor_field[3+ia], work, g_mu);
          xchange_field(g_spinor_field[3+ia]);
        }
      }  // of ia


      /* read 3 up-type propagators for spinor index gamma_perm(ir) */
      for(ia=0; ia<3; ia++) {
        if(!mms) {
          if(!use_shifted_spinor) {
            get_filename(filename, nu, 3*ir+ia, 1);
            exitstatus = read_lime_spinor(g_spinor_field[ia], filename, 0);
          } else {
            get_filename(filename, 4, 3*ir+ia, 1);
            exitstatus = read_lime_spinor(work, filename, 0);
            shift_spinor_field(g_spinor_field[ia], work, shift_vector);
          }
          if(exitstatus != 0) {
            fprintf(stderr, "\n# [avc_exact2_lowmem] Error from read_lime_spinor\n");
            exit(111);
          }
          xchange_field(g_spinor_field[ia]);
        } else {
          sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, nu, 3*ir+ia, mass_id);
          exitstatus = read_lime_spinor(work, filename, 0);
          if(exitstatus != 0) {
            fprintf(stderr, "\n# [avc_exact2_lowmem] Error from read_lime_spinor\n");
            exit(111);
          }
          xchange_field(work);
          Qf5(g_spinor_field[ia], work, -g_mu);
          xchange_field(g_spinor_field[ia]);
        }
      }  // of ia


      for(ia=0; ia<3; ia++) {
        phi = g_spinor_field[ia];
      for(ib=0; ib<3; ib++) {
        chi = g_spinor_field[3+ib];
        //fprintf(stdout, "\n# [nu5] spin index pair (%d, %d); col index pair (%d, %d)\n", ir, gperm[nu][ir], ia ,ib);
    
        // 1) gamma_nu gamma_5 x U^dagger
        for(mu=0; mu<4; mu++)
        //for(mu=0; mu<1; mu++)
        {

          imunu = 4*mu+nu;

#ifdef OPENMP
#pragma omp parallel for private(ix, spinor1, spinor2, U_, w, w1)  shared(imunu, ia, ib, nu, mu)
#endif
          for(ix=0; ix<VOLUME; ix++) {
            _cm_eq_cm_ti_co(U_, &g_gauge_field[_GGI(ix,mu)], &co_phase_up[mu]);

            _fv_eq_cm_ti_fv(spinor1, U_, phi+_GSI(g_iup[ix][mu]));
            _fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	    _fv_mi_eq_fv(spinor2, spinor1);
	    _fv_eq_gamma_ti_fv(spinor1, 5, spinor2);
	    _co_eq_fv_dag_ti_fv(&w, chi+_GSI(ix), spinor1);
            _co_eq_co_ti_co_conj(&w1, &w, (complex*)(Usource[nu]+2*(3*ib+ia)));
            if(!isimag[nu]) {
              conn[_GWI(imunu,ix,VOLUME)  ] += gperm_sign[nu][ir] * w1.re;
              conn[_GWI(imunu,ix,VOLUME)+1] += gperm_sign[nu][ir] * w1.im;
            } else {
              conn[_GWI(imunu,ix,VOLUME)  ] += gperm_sign[nu][ir] * w1.im;
              conn[_GWI(imunu,ix,VOLUME)+1] -= gperm_sign[nu][ir] * w1.re;
            }
            //if(ix==0) fprintf(stdout, "[2_nu5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib, w.re, w.im);
            //if(ix==0) fprintf(stdout, "[2_nu5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib,
            //    gperm_sign[nu][ir] * w1.re, gperm_sign[nu][ir] * w1.im);

          }  // of ix

#ifdef OPENMP
#pragma omp parallel for private(ix, spinor1, spinor2, U_, w, w1)  shared(imunu, ia, ib, nu, mu)
#endif
          for(ix=0; ix<VOLUME; ix++) {
            _cm_eq_cm_ti_co(U_, &g_gauge_field[_GGI(ix,mu)], &co_phase_up[mu]);

            _fv_eq_cm_dag_ti_fv(spinor1, U_, phi+_GSI(ix));
            _fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	    _fv_pl_eq_fv(spinor2, spinor1);
	    _fv_eq_gamma_ti_fv(spinor1, 5, spinor2);
	    _co_eq_fv_dag_ti_fv(&w, chi+_GSI(g_iup[ix][mu]), spinor1);
            _co_eq_co_ti_co_conj(&w1, &w, (complex*)(Usource[nu]+2*(3*ib+ia)));
            if(!isimag[nu]) {
              conn[_GWI(imunu,ix,VOLUME)  ] += gperm_sign[nu][ir] * w1.re;
              conn[_GWI(imunu,ix,VOLUME)+1] += gperm_sign[nu][ir] * w1.im;
            } else {
              conn[_GWI(imunu,ix,VOLUME)  ] += gperm_sign[nu][ir] * w1.im;
              conn[_GWI(imunu,ix,VOLUME)+1] -= gperm_sign[nu][ir] * w1.re;
            }
            //if(ix==0) fprintf(stdout, "[4_nu5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib, w.re, w.im);
            //if(ix==0) fprintf(stdout, "[4_nu5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib,
            //    gperm_sign[nu][ir] * w1.re, gperm_sign[nu][ir] * w1.im);

          }  // of ix
	} // of mu

      } /* of ib */
      } /* of ia */

      // read 3 dn-type propagators for spinor index id( ir )
      for(ia=0; ia<3; ia++) {
        if(!mms) {
          if(!up_dn_onefile) {
            get_filename(filename, 4, 3*gperm[4][ir]+ia, -1);
            exitstatus = read_lime_spinor(g_spinor_field[3+ia], filename, 0);
          } else {
            get_filename(filename, 4, 3*gperm[4][ir]+ia, 1);
            exitstatus = read_lime_spinor(g_spinor_field[3+ia], filename, 1);
          }
          if(exitstatus != 0) {
            fprintf(stderr, "\n# [avc_exact2_lowmem] Error from read_lime_spinor\n");
            exit(111);
          }
          xchange_field(g_spinor_field[3+ia]);
        } else {
          sprintf(filename, "%s.%.4d.%.2d.%.2d.cgmms.%.2d.inverted", filename_prefix, Nconf, 4, 3*gperm[4][ir]+ia, mass_id);
          exitstatus = read_lime_spinor(work, filename, 0);
          if(exitstatus != 0) {
            fprintf(stderr, "\n# [avc_exact2_lowmem] Error from read_lime_spinor\n");
            exit(111);
          }
          xchange_field(work);
          Qf5(g_spinor_field[3+ia], work, g_mu);
          xchange_field(g_spinor_field[3+ia]);
        }
      }  // of ia

      for(ia=0; ia<3; ia++) {
        phi = g_spinor_field[ia];
      for(ib=0; ib<3; ib++) {
        chi = g_spinor_field[3+ib];
        //fprintf(stdout, "\n# [5] spin index pair (%d, %d); col index pair (%d, %d)\n", ir, gperm[4][ir], ia ,ib);

        // -gamma_5 x U
        for(mu=0; mu<4; mu++)
        //for(mu=0; mu<1; mu++)
        {

          imunu = 4*mu+nu;

#ifdef OPENMP
#pragma omp parallel for private(ix, spinor1, spinor2, U_, w, w1)  shared(imunu, ia, ib, nu, mu)
#endif
          for(ix=0; ix<VOLUME; ix++) {
            _cm_eq_cm_ti_co(U_, &g_gauge_field[_GGI(ix,mu)], &co_phase_up[mu]);

            _fv_eq_cm_ti_fv(spinor1, U_, phi+_GSI(g_iup[ix][mu]));
            _fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	    _fv_mi_eq_fv(spinor2, spinor1);
	    _fv_eq_gamma_ti_fv(spinor1, 5, spinor2);
	    _co_eq_fv_dag_ti_fv(&w, chi+_GSI(ix), spinor1);
            _co_eq_co_ti_co_conj(&w1, &w, (complex*)(Usource[nu]+2*(3*ib+ia)));
            conn[_GWI(imunu,ix,VOLUME)  ] += gperm_sign[4][ir] * w1.re;
            conn[_GWI(imunu,ix,VOLUME)+1] += gperm_sign[4][ir] * w1.im;
            //if(ix==0) fprintf(stdout, "[2_5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib, w.re, w.im);
            //if(ix==0) fprintf(stdout, "[2_5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib,
            //    gperm_sign[4][ir] * w1.re, gperm_sign[4][ir] * w1.im);

          }  // of ix

#ifdef OPENMP
#pragma omp parallel for private(ix, spinor1, spinor2, U_, w, w1)  shared(imunu, ia, ib, nu, mu)
#endif
          for(ix=0; ix<VOLUME; ix++) {
            _cm_eq_cm_ti_co(U_, &g_gauge_field[_GGI(ix,mu)], &co_phase_up[mu]);

            _fv_eq_cm_dag_ti_fv(spinor1, U_, phi+_GSI(ix));
            _fv_eq_gamma_ti_fv(spinor2, mu, spinor1);
	    _fv_pl_eq_fv(spinor2, spinor1);
	    _fv_eq_gamma_ti_fv(spinor1, 5, spinor2);
	    _co_eq_fv_dag_ti_fv(&w, chi+_GSI(g_iup[ix][mu]), spinor1);
            _co_eq_co_ti_co_conj(&w1, &w, (complex*)(Usource[nu]+2*(3*ib+ia)));
            conn[_GWI(imunu,ix,VOLUME)  ] += gperm_sign[4][ir] * w1.re;
            conn[_GWI(imunu,ix,VOLUME)+1] += gperm_sign[4][ir] * w1.im;
            //if(ix==0) fprintf(stdout, "[4_5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib, w.re, w.im);
            //if(ix==0) fprintf(stdout, "[4_5] %3d%3d%3d\t%e\t%e\n", ir ,ia, ib,
            //    gperm_sign[4][ir] * w1.re, gperm_sign[4][ir] * w1.im);

          }  // of ix
	}  // of mu
      }  // of ib

      // contribution to contact term
      if(have_source_flag == g_cart_id)  {
        _fv_eq_cm_dag_ti_fv(spinor1, Usource[nu], phi+_GSI(source_location));
        _fv_eq_gamma_ti_fv(spinor2, nu, spinor1);
        _fv_pl_eq_fv(spinor2, spinor1);
        contact_term[2*nu  ] += 0.5 * spinor2[2*(3*ir+ia)  ];
        contact_term[2*nu+1] += 0.5 * spinor2[2*(3*ir+ia)+1];
      }
      }  // of ia 
    }  // of ir
  }  // of nu
  
  // print contact term
  if(g_cart_id == have_source_flag) {
    fprintf(stdout, "\n# [avc_exact2_lowmem] contact term\n");
    for(i=0;i<4;i++) {
      fprintf(stdout, "\t%d%25.16e%25.16e\n", i, contact_term[2*i], contact_term[2*i+1]);
    }
  }

  /* normalisation of contractions */
#ifdef OPENMP
#pragma omp parallel for
#endif
  for(ix=0; ix<32*VOLUME; ix++) conn[ix] *= -0.25;

#ifdef MPI
      retime = MPI_Wtime();
#else
      retime = (double)clock() / CLOCKS_PER_SEC;
#endif
  if(g_cart_id==0) fprintf(stdout, "contractions in %e seconds\n", retime-ratime);

  
  /* save results */
#ifdef MPI
  ratime = MPI_Wtime();
#else
  ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
  if(outfile_prefix_set) {
    sprintf(filename, "%s/avc2_v_x.%.4d", outfile_prefix, Nconf);
  } else {
    sprintf(filename, "avc2_v_x.%.4d", Nconf);
  }
  sprintf(contype, "cvc - cvc in position space, all 16 components");
  write_lime_contraction(conn, filename, 64, 16, contype, Nconf, 0);
#ifndef MPI
  if(write_ascii) {
    if(outfile_prefix_set) {
      sprintf(filename, "%s/avc2_v_x.%.4di.ascii", outfile_prefix, Nconf);
    } else {
      sprintf(filename, "avc2_v_x.%.4d.ascii", Nconf);
    }
    write_contraction(conn, NULL, filename, 16, 2, 0);
  }
#endif

#ifdef MPI
  retime = MPI_Wtime();
#else
  retime = (double)clock() / CLOCKS_PER_SEC;
#endif
  if(g_cart_id==0) fprintf(stdout, "saved position space results in %e seconds\n", retime-ratime);

#ifndef MPI
  /* check the Ward identity in position space */
  if(check_position_space_WI) {
    sprintf(filename, "WI_X.%.4d", Nconf);
    ofs = fopen(filename,"w");
    fprintf(stdout, "\n# [avc_exact2_lowmem] checking Ward identity in position space ...\n");
    for(x0=0; x0<T;  x0++) {
    for(x1=0; x1<LX; x1++) {
    for(x2=0; x2<LY; x2++) {
    for(x3=0; x3<LZ; x3++) {
      fprintf(ofs, "# t=%2d x=%2d y=%2d z=%2d\n", x0, x1, x2, x3);
      ix=g_ipt[x0][x1][x2][x3];
      for(nu=0; nu<4; nu++) {
        w.re = conn[_GWI(4*0+nu,ix,VOLUME)] + conn[_GWI(4*1+nu,ix,VOLUME)]
             + conn[_GWI(4*2+nu,ix,VOLUME)] + conn[_GWI(4*3+nu,ix,VOLUME)]
	     - conn[_GWI(4*0+nu,g_idn[ix][0],VOLUME)] - conn[_GWI(4*1+nu,g_idn[ix][1],VOLUME)]
	     - conn[_GWI(4*2+nu,g_idn[ix][2],VOLUME)] - conn[_GWI(4*3+nu,g_idn[ix][3],VOLUME)];

        w.im = conn[_GWI(4*0+nu,ix,VOLUME)+1] + conn[_GWI(4*1+nu,ix,VOLUME)+1]
            + conn[_GWI(4*2+nu,ix,VOLUME)+1] + conn[_GWI(4*3+nu,ix,VOLUME)+1]
	    - conn[_GWI(4*0+nu,g_idn[ix][0],VOLUME)+1] - conn[_GWI(4*1+nu,g_idn[ix][1],VOLUME)+1]
	    - conn[_GWI(4*2+nu,g_idn[ix][2],VOLUME)+1] - conn[_GWI(4*3+nu,g_idn[ix][3],VOLUME)+1];
      
        fprintf(ofs, "\t%3d%25.16e%25.16e\n", nu, w.re, w.im);
      }
    }}}}
    fclose(ofs);
  }
#endif

  /*********************************************
   * Fourier transformation 
   *********************************************/
#ifdef MPI
  ratime = MPI_Wtime();
#else
  ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
  for(mu=0; mu<16; mu++) {
    memcpy((void*)in, (void*)&conn[_GWI(mu,0,VOLUME)], 2*VOLUME*sizeof(double));
#ifdef MPI
    fftwnd_mpi(plan_p, 1, in, NULL, FFTW_NORMAL_ORDER);
#else
#  ifdef OPENMP
    fftwnd_threads_one(num_threads, plan_p, in, NULL);
#  else
    fftwnd_one(plan_p, in, NULL);
#  endif
#endif
    memcpy((void*)&conn[_GWI(mu,0,VOLUME)], (void*)in, 2*VOLUME*sizeof(double));
  }

#ifdef MPI
  if(g_cart_id==0) fprintf(stdout, "\n# [] broadcasing contact term ...\n");
  MPI_Bcast(contact_term, 8, MPI_DOUBLE, have_source_flag, g_cart_grid);
  fprintf(stdout, "[%2d] contact term = "\
      "(%e + I %e, %e + I %e, %e + I %e, %e +I %e)\n",
      g_cart_id, contact_term[0], contact_term[1], contact_term[2], contact_term[3],
      contact_term[4], contact_term[5], contact_term[6], contact_term[7]);
#endif

  /*****************************************
   * add phase factors
   *****************************************/
  for(mu=0; mu<4; mu++) {
    phi = conn + _GWI(5*mu,0,VOLUME);

    for(x0=0; x0<T; x0++) {
      phase[0] = 2. * (double)(Tstart+x0) * M_PI / (double)T_global;
    for(x1=0; x1<LX; x1++) {
      phase[1] = 2. * (double)(x1) * M_PI / (double)LX;
    for(x2=0; x2<LY; x2++) {
      phase[2] = 2. * (double)(x2) * M_PI / (double)LY;
    for(x3=0; x3<LZ; x3++) {
      phase[3] = 2. * (double)(x3) * M_PI / (double)LZ;
      ix = g_ipt[x0][x1][x2][x3];
      w.re =  cos( phase[0]*(sx0+Tstart)+phase[1]*sx1+phase[2]*sx2+phase[3]*sx3 );
      w.im = -sin( phase[0]*(sx0+Tstart)+phase[1]*sx1+phase[2]*sx2+phase[3]*sx3 );
      _co_eq_co_ti_co(&w1,(complex*)( phi+2*ix ), &w);
      phi[2*ix  ] = w1.re - contact_term[2*mu  ];
      phi[2*ix+1] = w1.im - contact_term[2*mu+1];
    }}}}
  }  /* of mu */

  for(mu=0; mu<3; mu++) {
  for(nu=mu+1; nu<4; nu++) {
    phi = conn + _GWI(4*mu+nu,0,VOLUME);
    chi = conn + _GWI(4*nu+mu,0,VOLUME);

    for(x0=0; x0<T; x0++) {
      phase[0] =  (double)(Tstart+x0) * M_PI / (double)T_global;
    for(x1=0; x1<LX; x1++) {
      phase[1] =  (double)(x1) * M_PI / (double)LX;
    for(x2=0; x2<LY; x2++) {
      phase[2] =  (double)(x2) * M_PI / (double)LY;
    for(x3=0; x3<LZ; x3++) {
      phase[3] =  (double)(x3) * M_PI / (double)LZ;
      ix = g_ipt[x0][x1][x2][x3];
      w.re =  cos( phase[mu] - phase[nu] - 2.*(phase[0]*(sx0+Tstart)+phase[1]*sx1+phase[2]*sx2+phase[3]*sx3) );
      w.im =  sin( phase[mu] - phase[nu] - 2.*(phase[0]*(sx0+Tstart)+phase[1]*sx1+phase[2]*sx2+phase[3]*sx3) );
      _co_eq_co_ti_co(&w1,(complex*)( phi+2*ix ), &w);
      phi[2*ix  ] = w1.re;
      phi[2*ix+1] = w1.im;

      w.re =  cos( phase[nu] - phase[mu] - 2.*(phase[0]*(sx0+Tstart)+phase[1]*sx1+phase[2]*sx2+phase[3]*sx3) );
      w.im =  sin( phase[nu] - phase[mu] - 2.*(phase[0]*(sx0+Tstart)+phase[1]*sx1+phase[2]*sx2+phase[3]*sx3) );
      _co_eq_co_ti_co(&w1,(complex*)( chi+2*ix ), &w);
      chi[2*ix  ] = w1.re;
      chi[2*ix+1] = w1.im;
    }}}}
  }}  /* of mu and nu */

#ifdef MPI
  retime = MPI_Wtime();
#else
  retime = (double)clock() / CLOCKS_PER_SEC;
#endif
  if(g_cart_id==0) fprintf(stdout, "Fourier transform in %e seconds\n", retime-ratime);

  /********************************
   * save momentum space results
   ********************************/
#ifdef MPI
  ratime = MPI_Wtime();
#else
  ratime = (double)clock() / CLOCKS_PER_SEC;
#endif
  if(outfile_prefix_set) {
    sprintf(filename, "%s/avc2_v_p.%.4d", outfile_prefix, Nconf);
  } else {
    sprintf(filename, "avc2_v_p.%.4d", Nconf);
  }
  sprintf(contype, "cvc - cvc in momentum space, all 16 components");
  write_lime_contraction(conn, filename, 64, 16, contype, Nconf, 0);
#ifndef MPI
  if(write_ascii) {
    if(outfile_prefix_set) {
      sprintf(filename, "%s/avc2_v_p.%.4d", outfile_prefix, Nconf);
    } else {
      sprintf(filename, "avc2_v_p.%.4d.ascii", Nconf);
    }
    write_contraction(conn, (int*)NULL, filename, 16, 2, 0);
  }
#endif

#ifdef MPI
  retime = MPI_Wtime();
#else
  retime = (double)clock() / CLOCKS_PER_SEC;
#endif
  if(g_cart_id==0) fprintf(stdout, "saved momentum space results in %e seconds\n", retime-ratime);

  if(check_momentum_space_WI) {
#ifdef MPI
    sprintf(filename, "WI_P.%.4d.%.2d", Nconf, g_cart_id);
#else
    sprintf(filename, "WI_P.%.4d", Nconf);
#endif  
    ofs = fopen(filename,"w");
    if(g_cart_id == 0) fprintf(stdout, "\n# [avc_exact2_lowmem] checking Ward identity in momentum space ...\n");
    for(x0=0; x0<T; x0++) {
      phase[0] = 2. * sin( (double)(Tstart+x0) * M_PI / (double)T_global );
    for(x1=0; x1<LX; x1++) {
      phase[1] = 2. * sin( (double)(x1) * M_PI / (double)LX );
    for(x2=0; x2<LY; x2++) {
      phase[2] = 2. * sin( (double)(x2) * M_PI / (double)LY );
    for(x3=0; x3<LZ; x3++) {
      phase[3] = 2. * sin( (double)(x3) * M_PI / (double)LZ );
      ix = g_ipt[x0][x1][x2][x3];
      fprintf(ofs, "# t=%2d x=%2d y=%2d z=%2d\n", x0, x1, x2, x3);
      for(nu=0;nu<4;nu++) {
        w.re = phase[0] * conn[_GWI(4*0+nu,ix,VOLUME)] + phase[1] * conn[_GWI(4*1+nu,ix,VOLUME)] 
             + phase[2] * conn[_GWI(4*2+nu,ix,VOLUME)] + phase[3] * conn[_GWI(4*3+nu,ix,VOLUME)];

        w.im = phase[0] * conn[_GWI(4*0+nu,ix,VOLUME)+1] + phase[1] * conn[_GWI(4*1+nu,ix,VOLUME)+1] 
             + phase[2] * conn[_GWI(4*2+nu,ix,VOLUME)+1] + phase[3] * conn[_GWI(4*3+nu,ix,VOLUME)+1];

        w1.re = phase[0] * conn[_GWI(4*nu+0,ix,VOLUME)] + phase[1] * conn[_GWI(4*nu+1,ix,VOLUME)] 
              + phase[2] * conn[_GWI(4*nu+2,ix,VOLUME)] + phase[3] * conn[_GWI(4*nu+3,ix,VOLUME)];

        w1.im = phase[0] * conn[_GWI(4*nu+0,ix,VOLUME)+1] + phase[1] * conn[_GWI(4*nu+1,ix,VOLUME)+1] 
              + phase[2] * conn[_GWI(4*nu+2,ix,VOLUME)+1] + phase[3] * conn[_GWI(4*nu+3,ix,VOLUME)+1];
        fprintf(ofs, "\t%d%25.16e%25.16e%25.16e%25.16e\n", nu, w.re, w.im, w1.re, w1.im);
      }
    }}}}
    fclose(ofs);
  }

  /****************************************
   * free the allocated memory, finalize
   ****************************************/
  free(g_gauge_field);
  for(i=0; i<no_fields; i++) free(g_spinor_field[i]);
  free(g_spinor_field);
  free_geometry();
  fftw_free(in);
  free(conn);
#ifdef MPI
  fftwnd_mpi_destroy_plan(plan_p);
  free(status);
  MPI_Finalize();
#else
  fftwnd_destroy_plan(plan_p);
#endif

  if(g_cart_id==0) {
    g_the_time = time(NULL);
    fprintf(stdout, "\n# [avc_exact2_lowmem] %s# [avc_exact2_lowmem] end of run\n", ctime(&g_the_time));
    fprintf(stderr, "\n# [avc_exact2_lowmem] %s# [avc_exact2_lowmem] end of run\n", ctime(&g_the_time));
  }

  return(0);

}

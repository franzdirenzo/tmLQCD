/* $Id$ */
/*******************************************************************************
*
* File hybrid.c
*
* Hybrid-Monte-Carlo for the pure gauge-theory
*
* Author: Martin Hasenbusch
* Date: Wed, Aug 29, 2001 02:06:26 PM
*
*******************************************************************************/

#define MAIN_PROGRAM

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include "global.h"
#include "su3.h"
#include "su3adj.h"
#include "expo.h"
#include "ranlxd.h"
#include "geometry_eo.h"
#include "start.h"
#include "linalg_eo.h"
#include "linsolve.h"
#include "clover_eo.h"
#include "observables.h"
#include "hybrid_update.h"
#include "xchange.h"
#include "sw.h"
#include "io.h"
#include "read_input.h"
#include "tm_operators.h"
#include "boundary.h"

int main(int argc,char *argv[]) {
 
  FILE *fp1=NULL,*fp2=NULL,*fp4=NULL;
  char * filename="output";
  char filename1[50];
  char filename2[50];
  char filename3[50];
  int rlxd_state[105];
  int idis0;
  int j,ix,mu;
  int i,k, nstore=0;
  double yy[1];
/*   static double step; */
/*   static double q_off,q_off2; */
  double eneg,enegx,enep,enepx,dh;
  double enerphi0,enerphi0x;
  static su3 gauge_tmp[VOLUME][4];
  su3 *v,*w;

  int Rate=0;
  int  namelen;
  char processor_name[MPI_MAX_PROCESSOR_NAME];
  verbose = 1;
  g_use_clover_flag = 0;
 
  
#ifdef MPI
  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &g_nproc);
  MPI_Comm_rank(MPI_COMM_WORLD, &g_proc_id);
  MPI_Get_processor_name(processor_name, &namelen);
  g_cart_grid = MPI_COMM_WORLD;

  fprintf(stdout,"Process %d of %d on %s\n",
	  g_proc_id, g_nproc, processor_name);
  fflush(stdout);
#else
  g_nproc = 1;
  g_proc_id = 0;
#endif

  /* Read the input file */
  read_input("hmc.input");
 
  q_off = 0.;
  q_off2 = 0.;
 
  if(g_proc_id == 0){
    fp7=fopen("solver_data", "w"); 
    
/*     fscanf(fp6,"%s",filename); */
    /*construct the filenames for the observables and the parameters*/
    strcpy(filename1,filename);  strcat(filename1,".data");
    strcpy(filename2,filename);  strcat(filename2,".para");
    
    fp1=fopen(filename1, "w");
    fp2=fopen(filename2, "w");
    
    printf("\n\nThe lattice size is %d x %d^3\n\n",(int)(T),(int)(L));
    printf("g_beta = %f , g_kappa= %f, g_kappa*csw/8= %f \n\n",g_beta,g_kappa,g_ka_csw_8);
    
    fprintf(fp2,"The lattice size is %d x %d^3\n\n",(int)(g_nproc*T),(int)(L));
    fprintf(fp2,"g_beta = %f , g_kappa= %f, g_kappa*csw/8= %f \n\n",g_beta,g_kappa,g_ka_csw_8);
    fprintf(fp2,"boundary %f %f %f %f \n \n",X0,X1,X2,X3);
    fprintf(fp2,"ITER_MAX_BCG=%d, EPS_SQ0=%e, EPS_SQ1=%e EPS_SQ2=%e, EPS_SQ3=%e \n\n"
	    ,ITER_MAX_BCG,EPS_SQ0,EPS_SQ1,EPS_SQ2,EPS_SQ3);
    fprintf(fp2,"q_off=%f, q_off2=%f, dtau=%f, Nsteps=%d, Nmeas=%d, Nskip=%d, integtyp=%d, nsmall=%d \n\n",
	    q_off,q_off2,dtau,Nsteps,Nmeas,Nskip,integtyp,nsmall);
    fprintf(fp2,"mu = %f", g_mu);
    
  }

  /* define the geometry */
  geometry();

  /* define the boundary conditions for the fermion fields */
  boundary();

#if defined GEOMETRIC
  if(g_proc_id==0) fprintf(fp2,"The geometric series is used as solver \n\n");
#else
  if(g_proc_id==0) fprintf(fp2,"The BICG_stab is used as solver \n\n");
#endif
  
  /* Continue */
  if(startoption == 3){
    if (g_proc_id == 0){
      fp4=fopen("rlxd_state","r");
      fread(rlxd_state,sizeof(rlxd_state),1,fp4);
      fclose(fp4);
      rlxd_reset(rlxd_state);
      printf("Reading Gauge field from file config\n"); fflush(stdout);
    }

    read_gauge_field_time_p("config");
  }
  else {
    /* Initialize random number generator */
    if(g_proc_id == 0) {
      rlxd_init(1, random_seed);
      /* hot */
      if(startoption == 1) {
	random_gauge_field();
      }
      rlxd_get(rlxd_state);
      MPI_Send(&rlxd_state[0], 105, MPI_INT, 1, 99, MPI_COMM_WORLD);
      MPI_Recv(&rlxd_state[0], 105, MPI_INT, g_nproc-1, 99, MPI_COMM_WORLD, &status);
      rlxd_reset(rlxd_state);
    }
    else {
      MPI_Recv(&rlxd_state[0], 105, MPI_INT, g_proc_id-1, 99, MPI_COMM_WORLD, &status);
      rlxd_reset(rlxd_state);
      /* hot */
      if(startoption == 1) {
	random_gauge_field();
      }
      k=g_proc_id+1; 
      if(k==g_nproc){
	k=0;
      }
      rlxd_get(rlxd_state);
      MPI_Send(&rlxd_state[0], 105, MPI_INT, k, 99, MPI_COMM_WORLD);
    }
    /* Cold */
    if(startoption == 0) {
      unit_g_gauge_field();
    }
    /* Restart */
    else if(startoption == 2) {
      read_gauge_field_time_p("config");
    }
  }

  /*For parallelization: exchange the gaugefield */
#ifdef MPI
  xchange_gauge();
#endif

  /*compute the energy of the gauge field*/
  eneg=measure_gauge_action();
  if(g_proc_id==0){
    fprintf(fp2,"%14.12f \n",eneg/(6.*VOLUME*g_nproc));
    fclose(fp2);
  }

  /* compute the energy of the determinant term */
  /* needed for exact continuation of the run, since evamax and eva use
     random numbers */ 
  if(startoption == 2 && g_proc_id == 0){
    rlxd_reset(rlxd_state);
  }
  
  /* set ddummy to zero */
  for(ix = 0; ix < VOLUME+RAND; ix++){
    for(mu=0; mu<4; mu++){
      ddummy[ix][mu].d1=0.;
      ddummy[ix][mu].d2=0.;
      ddummy[ix][mu].d3=0.;
      ddummy[ix][mu].d4=0.;
      ddummy[ix][mu].d5=0.;
      ddummy[ix][mu].d6=0.;
      ddummy[ix][mu].d7=0.;
      ddummy[ix][mu].d8=0.;
    }
  }

/*   unit_g_gauge_field(); */
/*   unit_spinor_field(2); */
/*   if(g_proc_id == 0) { */
/*     printf("%e %e %e %e %e\n", g_mu, spinor_field[2][0].c1.c1.re, spinor_field[2][0].c1.c1.im, spinor_field[2][0].c3.c1.re, spinor_field[2][0].c3.c1.im); */
/*   } */
/*   Qtm_plus_psi(0, 2);  */
/*   if(g_proc_id == 0) { */
/*     printf("%e %e %e %e %e\n", g_mu, spinor_field[0][0].c1.c1.re, spinor_field[0][0].c1.c1.im, spinor_field[0][0].c3.c1.re, spinor_field[0][0].c3.c1.im); */
/*     printf("%e %e %e %e %e\n", g_mu, spinor_field[0][6].c1.c1.re, spinor_field[0][6].c1.c1.im, spinor_field[0][6].c3.c1.re, spinor_field[0][6].c3.c1.im); */
/*   }   */


  
/*   MPI_Finalize(); */
/*   exit(0); */

  for(j=0;j<Nmeas;j++){
    /*copy the gauge field to gauge_tmp */
    for(ix=0;ix<VOLUME;ix++){ 
      for(mu=0;mu<4;mu++){
	v=&g_gauge_field[ix][mu];
	w=&gauge_tmp[ix][mu];
	_su3_assign(*w,*v);
      }
    }

    /* initialize the pseudo-fermion fields    */
    /* depending on q_off and q_off2 we use    */
    /* one, two or three pseudo-fermion fields */
    random_spinor_field(2);
    /* compute the square of the norm */
    enerphi0 = square_norm(2, VOLUME/2);

    /* apply the fermion matrix to the spinor */
    Qtm_plus_psi(0, 2);

    /* initialize the momenta */
    enep=ini_momenta();

    /*run the trajectory*/
    if(integtyp == 1){
      /* Leap-frog integration scheme */
      leap_frog(q_off, q_off2, dtau, Nsteps, nsmall); 
    }
    else if(integtyp == 2){
      /* Sexton Weingarten integration scheme */
      sexton(q_off, q_off2, dtau, Nsteps, nsmall);
    }

    /*perform the accept-reject-step*/
    enepx=moment_energy();
    enegx=measure_gauge_action();
    /*compute the energy contributions from the pseudo-fermions */

    zero_spinor_field(2);
    idis0=bicg(2, 0, q_off, EPS_SQ0);
    enerphi0x=square_norm(2, VOLUME/2);

    /* Compute the energy difference */
    dh=+enepx - g_beta*enegx - enep + g_beta*eneg
      + enerphi0x - enerphi0; 
      
    /* the random number is only taken at node zero and then distributed to 
       the other sites */
    if(g_proc_id==0){
      ranlxd(yy,1);
      for(i = 1; i < g_nproc; i++){
	MPI_Send(&yy[0], 1, MPI_DOUBLE, i, 31, MPI_COMM_WORLD);
      }
    }
    else{
      MPI_Recv(&yy[0], 1, MPI_DOUBLE, 0, 31, MPI_COMM_WORLD, &status);
    }
    if(exp(-dh) > yy[0]){
      /* accept */
      Rate += 1;
      eneg=enegx;
      /* put the links back to SU(3) group */
      for(ix=0;ix<VOLUME;ix++){ 
	for(mu=0;mu<4;mu++){ 
	  /* this is MIST */
	  v=&g_gauge_field[ix][mu];
	  *v=restoresu3(*v); 
	}
      }
    }
    else{
      /* reject: copy gauge_tmp to g_gauge_field */
      for(ix=0;ix<VOLUME;ix++){
	for(mu=0;mu<4;mu++){
	  /* Auch MIST */
	  v=&g_gauge_field[ix][mu];
	  w=&gauge_tmp[ix][mu];
	  _su3_assign(*v,*w);
	}
      }
    }
#ifdef MPI
    xchange_gauge();
#endif

    if(g_proc_id==0){
      fprintf(fp1,"%14.12f %14.12f %e %d %d %d \n",
	      eneg/(6.*VOLUME*g_nproc),dh,exp(-dh),
	      idis0,count00,count01);
      fflush(fp1);
    }
    /* Save gauge configuration all Nskip times */
    if((j+1)%Nskip == 0){
      sprintf(filename3,"%s.%.4d", filename, nstore);
      nstore ++;
      write_gauge_field_time_p( filename3 );

      /*  write the status of the random number generator on a file */
      if(g_proc_id==0){
	rlxd_get(rlxd_state);
	fp4=fopen("rlxd_state","w");
	fwrite(rlxd_state,sizeof(rlxd_state),1,fp4);
	fclose(fp4);
      }
    }
  }
  /* write the gauge configuration to the file gaugeconfig2 */
  write_gauge_field_time_p( "last_configuration" );
  
  if(g_proc_id==0){
    rlxd_get(rlxd_state);
    fp4=fopen("rlxd_state","w");
    fwrite(rlxd_state,sizeof(rlxd_state),1,fp4);
    fclose(fp4);
  }
  if(g_proc_id == 0){
    fprintf(stdout,"fertig \n");
    printf("Acceptance Rate was: %e\n", (double)Rate/(double)Nmeas);
    fflush(stdout);
  }
#ifdef MPI
  MPI_Finalize();
#endif
  return(0);
}

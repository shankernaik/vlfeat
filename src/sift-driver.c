/** @file     sift-driver.c
 ** @author   Andrea Vedaldi
 ** @brief    SIFT command line driver - Definition
 ** @internal
 **/

/* AUTORIGHTS */

#define VL_SIFT_DRIVER_VERSION_STRING "alpha-1"

#include "generic-driver.h"

#include <vl/generic.h>
#include <vl/stringop.h>
#include <vl/pgm.h>
#include <vl/sift.h>
#include <vl/getopt_long.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

/* ----------------------------------------------------------------- */
/* help message */
char const help_message [] =
  "Usage: %s [options] files ...\n"
  "\n"
  "Where options include:\n"
  " --verbose -v    Be verbose\n"
  " --help -h       Print this help message\n"
  " --version       Print version information\n"
  " --frames        Specify frames file\n"
  " --descriptors   Specify descriptors file\n"
  " --meta          Specify meta file\n"
  " --gss           Specify Gaussian scale space file\n"
  " --octaves -O    Number of octaves\n"
  " --levels -S     Number of levels per octave\n"
  " --first-octave  Index of the first octave\n"
  " --edges-tresh   Edges treshold\n"
  " --peaks-tresh   Peaks treshold\n"
  " --read-frames   Specify a file from which to read frames\n"
  " --orientations  Force the computation of the oriantations\n"
  "\n" ;

/* ----------------------------------------------------------------- */
/* long options codes */
enum { 
  opt_version = 1000, 
  opt_meta,
  opt_frames, 
  opt_descriptors,
  opt_gss,
  opt_first_octave,
  opt_edges_tresh,
  opt_peaks_tresh,
  opt_read_frames,
  opt_orientations
} ;

/* short options */
char const opts [] = "vhd:O:S" ;

/* long options */
struct option const longopts [] = {
  { "verbose",         no_argument,            0,          'v'              },
  { "help",            no_argument,            0,          'h'              },
  { "octaves",         required_argument,      0,          'O'              },
  { "levels",          required_argument,      0,          'S'              },
  { "version",         no_argument,            0,          opt_version      },
  { "meta",            optional_argument,      0,          opt_meta         },
  { "frames",          optional_argument,      0,          opt_frames       },
  { "descriptors",     optional_argument,      0,          opt_descriptors  },
  { "gss",             optional_argument,      0,          opt_gss          },
  { "first-octave",    required_argument,      0,          opt_first_octave },
  { "edges-tresh",     required_argument,      0,          opt_edges_tresh  },
  { "peaks-tresh",     required_argument,      0,          opt_peaks_tresh  },
  { "read-frames",     required_argument,      0,          opt_read_frames  },
  { "orientations",    required_argument,      0,          opt_orientations },
  { 0,                 0,                      0,          0                }
} ;

/* ----------------------------------------------------------------- */
/** @brief Save octave on disk
 **/

static int
save_gss (VlSiftFilt * filt, VlFileMeta * fm, const char * basename,
          int verbose)
{
  char tmp [1024] ;
  int S = filt -> S ;
  int q, i ;
  int s, err ;
  int w, h ;
  int o = filt -> o_cur ;
  VlPgmImage pim ;
  vl_uint8 *buffer = 0 ;

  if (! fm -> active) {
    return VL_ERR_OK ;
  }
  
  w = vl_sift_get_octave_width  (filt) ;
  h = vl_sift_get_octave_height (filt) ;
  
  pim.width     = w ;
  pim.height    = h ;
  pim.max_value = 255 ;
  pim.is_raw    = 1 ;
  
  buffer = malloc (sizeof(vl_uint8) * w * h) ;
  if (! buffer) {
    err = VL_ERR_ALLOC ;
    goto save_gss_quit ;
  }
  
  q = vl_string_copy (tmp, sizeof(tmp), basename) ;
  if (q >= sizeof(tmp)) {
    err = VL_ERR_OVERFLOW ;
    goto save_gss_quit ;
  }
  
  for (s = 0 ; s < S ; ++s) {
    vl_sift_pix * pt = vl_sift_get_octave (filt, s) ;
    
    /* conversion */
    for (i = 0 ; i < w * h ; ++i) {
      buffer [i] = (vl_uint8) pt [i] ;
    }
    
    /* save */
    snprintf(tmp + q, sizeof(tmp) - q, "_%02d_%03d", o, s) ;

    err = vl_file_meta_open (fm, tmp, "w") ;
    if (err) goto save_gss_quit ;    
    
    err = vl_pgm_insert (fm -> file, &pim, buffer) ;
    if (err) goto save_gss_quit ;

    if (verbose) {
      printf("sift: saved gss level to '%s'\n", fm -> name) ;
    }
    
    vl_file_meta_close (fm) ;
  }
    
 save_gss_quit : ;
  if (buffer) free (buffer) ;
  vl_file_meta_close (fm) ;
}

/* ----------------------------------------------------------------- */
/** @brief SIFT driver entry point 
 **/
int
main(int argc, char **argv)
{  
  /* algorithm parameters */ 
  double   edges_tresh  = 2.0 ;  
  double   peaks_tresh  = 2.0 ;  
  int      O = -1, S = 3, omin = -1 ;

  vl_bool  err    = VL_ERR_OK ;
  char     err_msg [1024] ;
  int      n ;
  int      exit_code = 0 ;
  int      verbose = 0 ;

  VlFileMeta frm  = {1, "%.frame", VL_PROT_ASCII, "", 0} ;
  VlFileMeta dsc  = {0, "%.descr", VL_PROT_ASCII, "", 0} ;
  VlFileMeta met  = {0, "%.meta",  VL_PROT_ASCII, "", 0} ;
  VlFileMeta gss  = {0, "%.pgm",   VL_PROT_ASCII, "", 0} ;
  VlFileMeta ifr  = {0, "%.frame", VL_PROT_ASCII, "", 0} ;
  
#define ERR(args...) {                                          \
    err = VL_ERR_BAD_ARG ;                                      \
    snprintf(err_msg, sizeof(err_msg), args) ;                  \
    break ;                                                     \
  }
  
  /* ------------------------------------------------------------------
   *                                                      Parse options
   * --------------------------------------------------------------- */
  while (!err) {
    int ch = getopt_long(argc, argv, "vhd:p", longopts, 0) ;

    /* end of option list? */
    if (ch == -1) break;

    /* process options */
    switch (ch) {

    case '?' :
      /* unkown option ............................................ */
      ERR("Invalid option '%s'.", argv [optind - 1]) ;
      break ;
      
    case ':' :
      /* missing argument ......................................... */
      ERR("Missing mandatory argument for option '%s'.", 
          argv [optind - 1]) ;
      break ;
   
    case 'h' :
      /* --help ................................................... */
      printf (help_message, argv [0]) ;
      exit (0) ;
      break ;

    case 'v' :
      /* --verbose ................................................ */
      ++ verbose ;
      break ;

    case opt_version :
      printf ("sift: driver version: %s libvl version: %s", 
              VL_SIFT_DRIVER_VERSION_STRING,
              vl_get_version_string()) ;
      exit(0) ;
      


    case opt_frames :
      /* --frames  ................................................ */
      err = vl_file_meta_parse (&frm, optarg) ;
      if (err) 
        ERR("The arguments of '%s' is invalid.", argv [optind - 1]) ;
      break ;

    case opt_descriptors :
      /* --descriptor ............................................. */
      err = vl_file_meta_parse (&dsc, optarg) ;
      if (err) 
        ERR("The arguments of '%s' is invalid.", argv [optind - 1]) ;
      break;
      
    case opt_meta :
      /* --meta ................................................... */
      err = vl_file_meta_parse (&met, optarg) ;      
      if (err) 
        ERR("The arguments of '%s' is invalid.", argv [optind - 1]) ;
      
      if (met.protocol != VL_PROT_ASCII)
        ERR("meta file supports only ASCII protocol") ;
      break ;

    case opt_read_frames :
      /* --meta ................................................... */
      err = vl_file_meta_parse (&ifr, optarg) ;      
      if (err) 
        ERR("The arguments of '%s' is invalid.", argv [optind - 1]) ;      
      break ;

    case opt_gss :
      /* --gss .................................................... */
      err = vl_file_meta_parse (&gss, optarg) ;
      if (err) 
        ERR("The arguments of '%s' is invalid.", argv [optind - 1]) ;
      break ;
    

    case 'O' :
      /* --octaves ............................................... */
      n = sscanf (optarg, "%d", &O) ;
      if (n == 0 || O < 0)
        ERR("The argument of '%s' must be a non-negative integer.",
            argv [optind - 1]) ;
      break ;
      
    case 'S' :
      /* --levels ............................................... */
      n = sscanf (optarg, "%d", &S) ;
      if (n == 0 || S < 0)
        ERR("The argument of '%s' must be a non-negative integer.",
            argv [optind - 1]) ;
      break ;

    case opt_first_octave :
      /* --first-octave ......................................... */
      n = sscanf (optarg, "%d", &omin) ;
      if (n == 0 || omin < 0)
        ERR("The argument of '%s' must be a non-negative integer.",
            argv [optind - 1]) ;
      break ;
      


    case opt_edges_tresh :
      /* --edge-tresh ........................................... */
      n = sscanf (optarg, "%lf", &edges_tresh) ;
      if (n == 0 || edges_tresh < 0)
        ERR("The argument of '%s' must be a non-negative float.",
            argv [optind - 1]) ;
      break ;

    case opt_peaks_tresh :
      /* --edge-tresh ........................................... */
      n = sscanf (optarg, "%lf", &peaks_tresh) ;
      if (n == 0 || peaks_tresh < 0)
        ERR("The argument of '%s' must be a non-negative float.",
            argv [optind - 1]) ;
      break ;

      
    case 0 :
    default :
      /* should not get here ...................................... */
      assert (0) ;
      break ;
    }
  }  
  
  /* check for parsing errors */
  if (err) {
    fprintf(stderr, "%s: error: %s (%d)\n", 
            argv [0], 
            err_msg, err) ;
    exit (1) ;
  }

  /* parse other arguments (filenames) */
  argc -= optind ;
  argv += optind ;

    if (verbose > 1) {
#define PRNFO(name,fm)                                                  \
      printf("sift: " name ": ") ;                                      \
      printf("active=%d ",  (fm).active ) ;                             \
      printf("pattern=%-10s ", (fm).pattern) ;                          \
      printf("protocol=%-6s ", vl_string_protocol_name ((fm).protocol)) ; \
      printf("\n") ;
      
      PRNFO("frames      ",frm) ;
      PRNFO("descriptors ",dsc) ;
      PRNFO("meta        ",met) ;
      PRNFO("gss         ",gss) ;
  }

  /* ------------------------------------------------------------------
   *                                         Process one image per time
   * --------------------------------------------------------------- */

  while (argc--) {

    char             basename [1024] ;
    char const      *name = *argv++ ;

    FILE            *in = 0 ;
    vl_uint8        *data = 0 ;
    vl_sift_pix     *fdata = 0 ;
    VlPgmImage       pim ;

    VlSiftFilt      *filt = 0 ;
    int              q ;
    vl_bool          done, first ;

    /* Determine files  ------------------------------------------ */
    
    /* get basenmae from filename */
    q = vl_string_basename (basename, sizeof(basename), name, 1) ;
    
    err = (q >= sizeof(basename)) ;

    if (err) {
      snprintf(err_msg, sizeof(err_msg), 
               "Basename of '%s' is too long", name);
      err = VL_ERR_OVERFLOW ;
      goto done ;
    }
    
    if (verbose) {
      printf("sift: processing '%s'\n", name) ;
    }
    
    if (verbose > 1) {
      printf("sift: basename is '%s'\n", basename) ;
    }

#define WERR(name)                                              \
    if (err == VL_ERR_OVERFLOW) {                               \
      snprintf(err_msg, sizeof(err_msg),                        \
               "Output file name too long.") ;                  \
      goto done ;                                               \
    } else if (err) {                                           \
      snprintf(err_msg, sizeof(err_msg),                        \
               "Could not open '%s' for writing.", name) ;      \
      goto done ;                                               \
    }

    /* open input file */
    in = fopen (name, "r") ;
    if (!in) {
      err = VL_ERR_IO ;
      snprintf(err_msg, sizeof(err_msg), 
               "Could not open '%s' for reading.", name) ;
      goto done ;
    }

    /* open output files */
    err = vl_file_meta_open (&dsc, basename, "w") ; WERR(dsc.name) ;    
    err = vl_file_meta_open (&frm, basename, "w") ; WERR(frm.name) ;   
    err = vl_file_meta_open (&met, basename, "w") ; WERR(met.name) ;

    if (verbose > 1) {
      if (dsc.active) printf("sift: writing descriptors to '%s'\n", dsc.name); 
      if (frm.active) printf("sift: writing frames to '%s'\n", frm.name); 
      if (met.active) printf("sift: writign meta to '%s'\n", met.name);
    }
    
    /* Read image data -------------------------------------------- */

    /* read source image header */
    err = vl_pgm_extract_head (in, &pim) ;
    if (err) {
      err = VL_ERR_IO ;
      snprintf(err_msg, sizeof(err_msg), 
               "PGM header corrputed.") ;
      goto done ;
    }
    
    if (verbose) {
      printf("sift: image is %d by %d pixels\n",
             pim. width,
             pim. height) ;
    }
    
    /* allocate buffer */
    data  = malloc(vl_pgm_get_data_size (&pim) * 
                   vl_pgm_get_bpp       (&pim)) ;
    fdata = malloc(vl_pgm_get_data_size (&pim) * 
                   vl_pgm_get_bpp       (&pim) * sizeof (vl_sift_pix)) ;
    
    if (!data || !fdata) {
      err = VL_ERR_ALLOC ;
      snprintf(err_msg, sizeof(err_msg), 
               "Could not allocate enough memory.") ;
      goto done ;
    } 
    
    /* read PGM */
    err  = vl_pgm_extract_data (in, &pim, data) ;
    if (err) {
      snprintf(err_msg, sizeof(err_msg), 
               "PGM body corrputed.") ;
      goto done ;
    }

    /* convert data type */
    for (q = 0 ; q < pim.width * pim.height ; ++q)
      fdata [q] = data [q] ;
    
    /* Optionally read keypoint file  ----------------------------- */

    /* Process data  ---------------------------------------------- */

    filt = vl_sift_new (pim.width, pim.height, O, S, omin) ;
    if (!filt) {
      snprintf(err_msg, sizeof(err_msg),
               "Could not allocate SIFT filter.") ;
      err = VL_ERR_ALLOC ;
      goto done ;
    }
    
    done  = 0 ;
    first = 1 ;
    while (1) {
      VlSiftKeypoint const *keys ;
      int                   nkeys, k ;
      
      /* process next octave */
      if (first) {
        first = 0 ;
        err = vl_sift_process_first_octave (filt, fdata) ;
      } else {
        err = vl_sift_process_next_octave  (filt) ;
      }
      
      if (err) {
        err = VL_ERR_OK ;
        break ;
      }

      if (verbose > 1) {
        printf("sift: next octave\n" );
      }

      /* optionally save GSS */
      if (gss.active) {
        err = save_gss (filt, &gss, basename, verbose) ;
        if (err) {
          snprintf (err_msg, sizeof(err_msg),
                    "Could not write GSS level to PGM file.") ;
          goto done ;
        }
      }

      /* run detector */
      vl_sift_detect (filt) ;
      keys  = vl_sift_get_keypoints (filt) ;
      nkeys = vl_sift_get_keypoints_num (filt) ;

      if (verbose > 1) {
        printf ("sift: %d keypoints\n", nkeys) ;
      }

      /* for each keypoint */
      for (k = 0 ; k < nkeys ; ++k) {
        double angles [4] ;
        int    nangles ;
        nangles = 
          vl_sift_calc_keypoint_orientations (filt, angles, keys + k) ;
        
        /* for each orientation */
        for (q = 0 ; q < nangles ; ++q) {
          vl_sift_pix descr [128] ;
          vl_sift_calc_keypoint_descriptor (filt, descr, keys + k, angles [q]) ;
          
          if (frm.active) {
            fprintf(frm.file, "%g %g %g %g\n", 
                    keys->x, keys->y, keys->sigma, angles [q])  ;
          }
          
          if (dsc.active) {
            int i ;
            for (i = 0 ; i < 128 ; ++i) {
              fprintf(dsc.file, "%g ", descr [i]) ;
            }
            fprintf(dsc.file, "\n") ;
          }
        }
      }
    }

    /* Meta file -------------------------------------------------- */
        
    if (met.active) {
      fprintf(met.file, "<sift\n") ;
      fprintf(met.file, "  input       = '%s'\n", name) ;
      if (dsc.active) {
        fprintf(met.file, "  descriptors = '%s'\n", dsc.name) ;
      }
      if (frm.active) {
        fprintf(met.file,"  frames      = '%s'\n", frm.name) ;
      }
      fprintf(met.file, ">\n") ;
    }

    /* Next guy  ----------------------------------------------- */
  done :
    /* release filter */
    if (filt) {
      vl_sift_delete (filt) ;
      filt = 0 ;
    }
  
    /* release image data */
    if (data) {
      free (data) ;
      data = 0 ;
    }

    /* close files */
    if (in) {
      fclose (in) ;
      in = 0 ;
    }

    vl_file_meta_close (&frm) ;
    vl_file_meta_close (&dsc) ;
    vl_file_meta_close (&met) ;
    vl_file_meta_close (&gss) ;
        
    /* if bad print error message */
    if (err) {
      fprintf
        (stderr,
         "sift: err: %s (%d)\n",
         err_msg,
         err) ;
      exit_code = 1 ;
    }
  }

  /* quit */
  return exit_code ;
}


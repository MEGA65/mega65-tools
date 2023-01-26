#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>

#define PNG_DEBUG 3
#include <png.h>

#include <cairo.h>
#include <cairo-pdf.h>

int *values;
double *times;
int val_count=0;
int val_allocated=0;

int ts_mult=0;
char ts_units[1024];
int next_is_ts=0;
float ts_div=1.0;

#define MAX_SIGS 16
int sig_count=0;
char sig_names[MAX_SIGS][1024];
char sig_desigs[MAX_SIGS][1024];
int sig_widths[MAX_SIGS];
int sig_firstbits[MAX_SIGS];

char sig_name[1024];
char sig_designator[1024];
int sig_firstbit,sig_lastbit,sig_width;

void abort_(const char * s, ...)
{
        va_list args;
        va_start(args, s);
        vfprintf(stderr, s, args);
        fprintf(stderr, "\n");
        va_end(args);
        abort();
}

void parse_ts(void)
{
  // Normalise time units into nsec
  if (!strcmp(ts_units,"fs")) ts_div=1000000;
  if (!strcmp(ts_units,"ps")) ts_div=1000;
  if (!strcmp(ts_units,"ns")) ts_div=1;
  if (!strcmp(ts_units,"us")) ts_div=0.001;
  if (!strcmp(ts_units,"ms")) ts_div=0.000001;
  ts_div/=ts_mult;
  fprintf(stderr,"INFO: Timestep divisor is %f\n",ts_div);
}

void draw_line(cairo_t *cr, float x1, float y1, float x2, float y2)
{
  cairo_set_line_width(cr, 0.5);
  cairo_move_to(cr, x1, y1);
  cairo_line_to(cr, x2, y2);
  cairo_stroke(cr);
}  

int raster_len=0;
int rasters=0;

int set_pixel(png_bytep *png_rows, int x, int y, int r, int g, int b)
{
  if (y < 0 || y>=rasters ) {
    //    printf("set_pixel: y=%d out of bounds", y);
    return 1;
  }
  if (x < 0 || x > raster_len) {
    //    printf("set_pixel: x=%d out of bounds", x);
    return 1;
  }

  //  log_debug("Setting pixel at %d,%d to #%02x%02x%02x",x,y,b,g,r);
  ((unsigned char *)png_rows[y])[x * 3 + 0] = r;
  ((unsigned char *)png_rows[y])[x * 3 + 1] = g;
  ((unsigned char *)png_rows[y])[x * 3 + 2] = b;

  return 0;
}

int main(int argc,char **argv) 
{
  if (argc<3) {
    fprintf(stderr,"usage: vcdgraph <vcd input file> <pdf output file> [signal names...]\n");
    exit(-1);
  }

  val_allocated=100000000;
  values = calloc(sizeof(int),val_allocated);
  times = calloc(sizeof(double),val_allocated);
  if ((!values)||(!times)) {
    fprintf(stderr,"ERROR: Failed to allocate arrays for measurements.\n");
    exit(-1);
  }
  
  
  FILE *f=fopen(argv[1],"r");
  if (!f) {
    fprintf(stderr,"ERROR: Could not read from '%s'\n",argv[1]);
    exit(-1);
  }

  char line[1024];

  int in_setup=1;

  double ts=0;
  
  int lines=0;
  line[0]=0; fgets(line,1024,f);
  while(line[0]) {

    if (in_setup) {

      if (next_is_ts) {
	next_is_ts=0;
	if (sscanf(line,"%d %s",&ts_mult,ts_units)==2) {
	  fprintf(stderr,"INFO: Set time scale to x %d %s\n",ts_mult,ts_units);
	  parse_ts();
	}
      }
      else if (sscanf(line,"$timescale %d %s $end",&ts_mult,ts_units)==2) {
	fprintf(stderr,"INFO: Set time scale to x %d %s\n",ts_mult,ts_units);
	parse_ts();
      }
      else if (!strncmp("$timescale",line,strlen("$timescale"))) next_is_ts=1;
      else if (sscanf(line,"$var reg %d %s %[^[][%d:%d] $end",
		      &sig_width,sig_designator,sig_name,&sig_firstbit,&sig_lastbit)==5) {
	for(int i=3;i<argc;i++) {
	  if (!strcmp(sig_name,argv[i])) {
	    fprintf(stderr,"INFO: Signal '%s' is designated by '%s'\n",sig_name,sig_designator);
	    if (sig_count<MAX_SIGS) {
	      strcpy(sig_names[sig_count],sig_name);
	      strcpy(sig_desigs[sig_count],sig_designator);
	      sig_widths[sig_count]=sig_width;
	      sig_firstbits[sig_count]=sig_firstbit;
	      sig_count++;
	    } else {
	      fprintf(stderr,"ERROR: Too many signals. Increase MAX_SIGS or select fewer signals.\n");
	      exit(-1);
	    }
	  }
	}
      }
      if (!strncmp("$enddefinitions",line,strlen("$enddefinitions"))) {
	in_setup=0;
	fprintf(stderr,"INFO: Found %d signals.\n",sig_count);
	if (sig_count!=(argc-3)) {
	  fprintf(stderr,"ERROR: Expected to see %d signals.  Are some of the names incorrect?\n",argc-2);
	  exit(-1);
	}
      }
    } else {
      long long ts_raw;
      if (sscanf(line,"#%lld",&ts_raw)==1) {
	ts=ts_raw/ts_div;
	//	fprintf(stderr,"DEBUG: ts=%g\n",ts);
      }
      // Remove CRLF
      while(line[0]&&line[strlen(line)-1]<' ') line[strlen(line)-1]=0;
      for(int sig=0;sig<sig_count;sig++) {
	if (!strcmp(&line[strlen(line)-strlen(sig_desigs[sig])],sig_desigs[sig])) {
	  if (line[0]=='b') {
	    // Binary string
	    int v=0;
	    for(int i=1;i<strlen(line);i++) {
	      if (line[i]==' ') break;
	      v=v*2;
	      switch(line[i]) {
	      case 'U': case 'X': case '0': v+=0; break;
	      case '1': case 'H': v+=1; break;
	      }
	    }
	    //	    fprintf(stderr,"DEBUG: Value for %s at ts %g : v=%d\n",sig_names[sig],ts,v);
	    if (val_count<val_allocated) {
	      values[val_count]=v;
	      times[val_count++]=ts;
	    } else {
	      fprintf(stderr,"WARNING: Too many measurements. Stopping at ts = %g ns\n",ts);
	      break;
	    }
	  } else {
	    fprintf(stderr,"ERROR: Unknown data format '%s'\n",line);
	  }
	}
      }
    }
      
    
    lines++;
    
    line[0]=0; fgets(line,1024,f);
  }

  fprintf(stderr,"INFO: Read %d values\n",val_count);
  
  fclose(f);
  fprintf(stderr,"INFO: Processed %d lines\n",lines);
  
  cairo_surface_t *surface;
  cairo_t *cr;

  int page_height=842;
  int page_width=595;
  
  surface = cairo_pdf_surface_create("pdffile.pdf", page_width, page_height);
  cr = cairo_create(surface);

  cairo_set_source_rgb(cr, 0, 0, 0);

#if 0
  cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
      CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size (cr, 40.0);

  cairo_move_to(cr, 10.0, 50.0);
  cairo_show_text(cr, "Disziplin ist Macht.");
#endif

#define ROW_HEIGHT (72.0/4)
#define ROW_GAP (72.0/16)
  // C64 style PAL is 63usec, although official PAL is 64usec.
  // C64 thus actually runs slightly faster than 50Hz display
  //  #define RASTER_DURATION 64040.0
  #define RASTER_DURATION 16010.0
  int page_y_margin=72/4;
  int page_x_margin=72/4;
  int page_y=page_y_margin;

  int raster_num=0;

  int val_num=0;
  int val=0;

  // Now used to track time point during rendering
  ts=0.0;

  while(raster_num < 312.5*2 ) {
    fprintf(stderr,"INFO: Page starting on raster %d\n",raster_num);
    page_y=page_y_margin;
    for(;page_y<(page_height-page_y_margin);page_y+=ROW_HEIGHT) {

      cairo_set_source_rgb(cr, 0, 0, 0);
      
      cairo_select_font_face (cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
			      CAIRO_FONT_WEIGHT_NORMAL);
      cairo_set_font_size (cr, (ROW_HEIGHT - ROW_GAP) * 1.0 );
      cairo_move_to(cr, page_x_margin, page_y + ROW_HEIGHT - ROW_GAP);
      char msg[1024];
      snprintf(msg,1024,"%d",raster_num);
      cairo_show_text(cr, msg);
            
      int x_range = (page_width - page_x_margin) - (page_x_margin+72/2);
      double ts_step = RASTER_DURATION / (1.0*x_range);

      // Draw yellow behind sync voltage range
      cairo_set_source_rgb(cr, 1.0, 1.0, 0);
      cairo_rectangle(cr, page_x_margin+72/2, page_y+0.7*(ROW_HEIGHT - ROW_GAP),
		      page_width - 2*page_x_margin-72/2, 0.3*(ROW_HEIGHT - ROW_GAP));
      cairo_fill(cr);
      // Draw green behind image voltage range
      cairo_set_source_rgb(cr, 0.0, 1.0, 0);
      cairo_rectangle(cr, page_x_margin+72/2, page_y+0.2*(ROW_HEIGHT - ROW_GAP),
		      page_width - 2*page_x_margin-72/2, 0.5*(ROW_HEIGHT - ROW_GAP));
      cairo_fill(cr);
      // Draw orange behind upper part of image voltage range (margin for colour burst)
      cairo_set_source_rgb(cr, 0.8, 0.5, 0);
      cairo_rectangle(cr, page_x_margin+72/2, page_y+0.0*(ROW_HEIGHT - ROW_GAP),
		      page_width - 2*page_x_margin-72/2, 0.2*(ROW_HEIGHT - ROW_GAP));
      cairo_fill(cr);
      
      cairo_set_source_rgb(cr, 0, 0, 0);
      for(int x=1;x<x_range;x++) {
	int prev_val=val;
	ts+=ts_step;
	
	// Advance to the next measurement, if required
	while((val_num<val_count)&&times[val_num+1]<ts) val_num++;
	val=values[val_num];
	
	draw_line(cr,page_x_margin+72/2+(x-1),page_y+ROW_HEIGHT-ROW_GAP-prev_val/256.0*(ROW_HEIGHT - ROW_GAP),
		  page_x_margin+72/2+x,page_y+ROW_HEIGHT-ROW_GAP-val/256.0*(ROW_HEIGHT - ROW_GAP));
	
      }
      
      raster_num++;
    }
    
    cairo_show_page(cr);
  }

  cairo_surface_destroy(surface);
  cairo_destroy(cr);

  fprintf(stderr,"Writing raw sample file to samples.raw\n");
  ts=0.0;
  // 27MHz apparent sample rate
  double ts_step = 1000.0/27;  
  val_num=0;
  int sample_num=0;
#define MAX_SAMPLES 8000000
  unsigned char samples[MAX_SAMPLES];
  
  while((val_num<val_count)&&sample_num<MAX_SAMPLES)
    {
      ts+=ts_step;
      
      // Advance to the next measurement, if required
      while((val_num<val_count)&&times[val_num+1]<ts) val_num++;
      val=values[val_num];
      
      samples[sample_num++]=val;
      
    }
  
  
  f=fopen("samples.raw","wb");
  fwrite(samples,1,sample_num,f);
  fclose(f);

  // Now decode the samples into a simple PNG format to visualise
  printf("Searching through %d samples...\n",sample_num);
  int sync_hyst[8192];
  int sync_len_hyst[8192];
  int best_hyst=0;
  int best_len_hyst=0;
  int last_hsync=0;
  int last_hsync_end=0;
  for(int i=0;i<8192;i++) sync_hyst[i]=0;
  for(int i=1;i<sample_num;i++) {
    if ((samples[i]==0x00)&&(samples[i-1]>0x20)) {
      // Start of sync
      sync_hyst[i-last_hsync]++;
      if (sync_hyst[i-last_hsync]>sync_hyst[best_hyst]) {
	best_hyst=i-last_hsync;
      }
      last_hsync=i;
    }

    if ((samples[i]>0x20)&&(samples[i-1]==0x00)) {
      // End of sync
      sync_len_hyst[i-last_hsync_end]++;
      if (sync_len_hyst[i-last_hsync_end]>sync_len_hyst[best_len_hyst]) {
	best_len_hyst=i-last_hsync_end;
      }
      last_hsync_end=i;
    }
    
  }
  printf("Raster length is probably %d samples.\n",best_hyst);
  printf("Sync length is probably %d samples.\n",best_len_hyst);
  raster_len=best_hyst;
  rasters = sample_num/raster_len + 1;
  
  png_structp png_ptr;
  png_infop info_ptr;

  /* create PNG file */

  png_bytep png_rows[rasters];
  for (int y=0;y<rasters;y++)
    png_rows[y] = (png_bytep)calloc(3 * raster_len,sizeof(png_byte)); 

  // Grey-scale pixels
  int y=0;
  int x=0;
  int first_sync=0;
  for(int i=1;i<sample_num;i++) {
    if (samples[i]==0&&(samples[i-1]>0x20)) {
      first_sync=i;
      break;
    }
  }

  int last_sync=0;
  for(int i=first_sync;i<sample_num;i++) {
    set_pixel(png_rows,x,y,
	      samples[i],samples[i],samples[i]);
    x++;
    if (x>=raster_len) {
      x=0; y++;
    } else if (i&&samples[i]==0&&(samples[i-1]>0x20)) {
      printf("SYNC end after %d px\n",x);
      if ((i-last_sync)==raster_len-1) {
	x=0; y++;
      }
      last_sync=i;
    }
  }
  
  char *file_name="rasters.png";
  FILE *fp = fopen(file_name, "wb");
  if (!fp)
    abort_("[write_png_file] File %s could not be opened for writing", file_name);
  
  
  /* initialize stuff */
  png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  
  if (!png_ptr)
    abort_("[write_png_file] png_create_write_struct failed");
  
  info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr)
    abort_("[write_png_file] png_create_info_struct failed");
  
  if (setjmp(png_jmpbuf(png_ptr)))
    abort_("[write_png_file] Error during init_io");
  
  png_init_io(png_ptr, fp);
  
  
  /* write header */
  if (setjmp(png_jmpbuf(png_ptr)))
    abort_("[write_png_file] Error during writing header");
  
  png_set_IHDR(png_ptr, info_ptr, raster_len, rasters, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
      PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
  
  png_write_info(png_ptr, info_ptr);
  
  
  /* write bytes */
  if (setjmp(png_jmpbuf(png_ptr)))
    abort_("[write_png_file] Error during writing bytes");
  
  png_write_image(png_ptr, png_rows);
  
  
  /* end write */
  if (setjmp(png_jmpbuf(png_ptr)))
    abort_("[write_png_file] Error during end of write");
  
  png_write_end(png_ptr, NULL);
  
  fclose(fp);

  // Now generate a test pattern image with PAL colour modulation
  // to compare with what the VHDL produces

  for (int y=0;y<rasters;y++)
    png_rows[y] = (png_bytep)calloc(3 * raster_len,sizeof(png_byte)); 
  
  printf("Image size = %dx%d\n",raster_len,rasters);
  
  for(int y=0;y<rasters;y++) {
    for(int x=0;x<raster_len;x++) {
      // Create the same transition as in the middle section of the
      // VHDL test pattern, that lacks chroma intensity when R > G
      float r=y/256.0;
      float g=x/256.0;
      float b=1;

      /* Y = 0.299R´ + 0.587G´ + 0.114B´
	 U = – 0.147R´ – 0.289G´ + 0.436B´
	 = 0.492 (B´ – Y)
	 V = 0.615R´ – 0.515G´ – 0.100B´
	 = 0.877(R´ – Y)
      */

      float y=0.299*r + 0.587*g + 0.114 * b;
      float u = -.147*r - 0.289*g - 0.436*b;
      float v = 0.615*r - 0.515*g - 0.100*b;

      float luma = y + 0.3;
      float chroma = 0;
      float comp = luma + chroma;
      
      set_pixel(png_rows,x,y, comp*255, comp*255, 255 + 0* comp*255);
    }
  }

  return 0;
}

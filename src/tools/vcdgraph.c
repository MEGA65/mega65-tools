#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>

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
#define RASTER_DURATION 64040.0
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

  return 0;
}

#include <u.h>

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_LCD_FILTER_H

#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include "a.h"

static FcConfig    *fc;
static FT_Library  lib;
static int	   aa_flags = FT_LOAD_TARGET_NORMAL;
static int	   linegap = 2;

void
loadfonts(void)
{
	int i;
	FT_Error e;
	FcFontSet *sysfonts;

	if((fc=FcInitLoadConfigAndFonts()) == NULL) {
		fprint(2, "fontconfig initialization failed\n");
		exits("fontconfig failed");
	}

	e = FT_Init_FreeType(&lib);
	if(e) {
		fprint(2, "freetype initialization failed: %d\n", e);
		exits("freetype failed");
	}

	sysfonts = FcConfigGetFonts(fc, FcSetSystem);

	xfont = emalloc9p(sysfonts->nfont*sizeof xfont[0]);
	memset(xfont, 0, sysfonts->nfont*sizeof xfont[0]);
	for(i=0; i<sysfonts->nfont; i++) {
		FcChar8 *fullname, *fontfile;
		int index;
		FcPattern *pat = sysfonts->fonts[i];

		if(FcPatternGetString(pat, FC_FULLNAME, 0, &fullname) != FcResultMatch ||
		   FcPatternGetString(pat, FC_FILE, 0, &fontfile) != FcResultMatch     ||
		   FcPatternGetInteger(pat, FC_INDEX, 0, &index) != FcResultMatch)
			continue;

		xfont[nxfont].name     = strdup((char*)fullname);
		xfont[nxfont].fontfile = strdup((char*)fontfile);
		xfont[nxfont].index    = index;
		nxfont++;
	}

	FcFontSetDestroy(sysfonts);
}

void
load(XFont *f)
{
	FT_Face face;
	FT_Error e;
	FT_ULong charcode;
	FT_UInt glyph_index;

	if(f->loaded)
		return;

	e = FT_New_Face(lib, f->fontfile, f->index, &face);

	if(e){
		fprint(2, "load failed for %s (%s) index:%d\n", f->name, f->fontfile, f->index);
		return;
	}

	if(!FT_IS_SCALABLE(face)) {
		fprint(2, "%s is a non scalable font, skipping\n", f->name);
		FT_Done_Face(face);
	    f->loaded = 1;
		return;
	}

	f->height = face->height;
	f->originy = face->descender;
	f->unit = face->units_per_EM;

	for(charcode=FT_Get_First_Char(face, &glyph_index); glyph_index != 0;
		charcode=FT_Get_Next_Char(face, charcode, &glyph_index)) {

		int idx = charcode/SubfontSize;

		if(charcode > 0xffff)
			break;

		if(!f->range[idx]) {
			f->range[idx] = 1;
			f->nrange++;
		}
	}

	FT_Done_Face(face);
	f->loaded = 1;
}

Memsubfont*
mksubfont(XFont *xf, char *name, int lo, int hi, int size, int antialias)
{
	FT_Face face;
	FT_Error e;
	Memimage *m, *mc, *m1;
	int x, y, y0;
	int descent, height;
	int i;
	Fontchar *fc, *fc0;
	Memsubfont *sf;

	e = FT_New_Face(lib, xf->fontfile, xf->index, &face);

	if(e){
		fprint(2, "load failed for %s (%s) index:%d\n", xf->name, xf->fontfile, xf->index);
		return nil;
	}

	/* Manipulate the incoming size parameter by repeating the calculation done in the main fontsrv.
 	   fontsrv believes this to be the pixel height, so set the font at that pixel height as well
 	*/
 	size = xf->height * (int)size/xf->unit + 0.99999999;

	//fprint(2, "Size: %d\n", size);

	/* Let Freetype2 render the font at a given pixel size */
	e = FT_Set_Pixel_Sizes(face, 0, size - linegap);
	if(e){
		fprint(2, "FT_Set_Pixel_Sizes failed\n");
		FT_Done_Face(face);
		return nil;
	}

	x = face->size->metrics.max_advance;
	descent = face->size->metrics.descender>>6;
	height = face->size->metrics.height>>6;

	y = height;
	y0 = -descent;

	m = allocmemimage(Rect(0, 0, x*(hi+1-lo), y), antialias ? GREY8 : GREY1);
	if(m == nil) {
		FT_Done_Face(face);
		return nil;
	}
	mc = allocmemimage(Rect(0, 0, x, y), antialias ? GREY8 : GREY1);
	if(mc == nil) {
		freememimage(m);
		FT_Done_Face(face);
		return nil;
	}
	memfillcolor(m, DBlack);
	memfillcolor(mc, DBlack);
	fc = malloc((hi+2 - lo) * sizeof fc[0]);
	sf = malloc(sizeof *sf);
	if(fc == nil || sf == nil) {
		freememimage(m);
		freememimage(mc);
		free(fc);
		free(sf);
		FT_Done_Face(face);
		return nil;
	}
	fc0 = fc;

	x = 0;
	for(i=lo; i<=hi; i++, fc++) {
		int r;
		int advance;

		memfillcolor(mc, DBlack);

		e = FT_Load_Char(face, i, FT_LOAD_RENDER|FT_LOAD_NO_HINTING|(antialias ? 0:FT_LOAD_TARGET_MONO));
		if(e){
			fprint(2, "FT_Load_Char failed for %d\n", i);
			//mempoly(mc, rect_points, 4, Endsquare, Endsquare, 0, memopaque, ZP, S);
			memimageline(mc, m->r.min, Pt(m->r.max.x, m->r.min.y), Endsquare, Endsquare, 0, memopaque, ZP, S);
			memimageline(mc, m->r.min, Pt(m->r.min.x, m->r.max.y), Endsquare, Endsquare, 0, memopaque, ZP, S);
			memimageline(mc, Pt(m->r.max.x, m->r.min.y), m->r.max, Endsquare, Endsquare, 0, memopaque, ZP, S);
			memimageline(mc, Pt(m->r.min.x, m->r.max.y), m->r.max, Endsquare, Endsquare, 0, memopaque, ZP, S);
			memimageline(mc, m->r.min, m->r.max, Endsquare, Endsquare, 0, memopaque, ZP, S);
			advance = Dx(m->r);

			memimagedraw(m, Rect(x, 0, x + advance, y), mc, ZP, memopaque, ZP, S);
		} else {
			FT_Bitmap *bitmap = &face->glyph->bitmap;
			uchar *base = byteaddr(mc, mc->r.min);
			advance = (face->glyph->advance.x+32) >> 6;

			for(r=0; r < bitmap->rows; r++)
				memmove(base + r*mc->width*sizeof(u32int), bitmap->buffer + r*bitmap->pitch, bitmap->pitch);

			//fprint(2, "bitmap_left: %d, bitmap_top: %d\n", face->glyph->bitmap_left, face->glyph->bitmap_top);
			memimagedraw(m, Rect(x, 0, x + advance, y), mc,
				Pt(-face->glyph->bitmap_left, -(y - y0 - face->glyph->bitmap_top)),
				memopaque, ZP, S);
		}

		fc->x = x;
		fc->top = 0;
		fc->bottom = y;
		fc->left = 0;
		fc->width = advance;
		x += advance;

#ifdef DEBUG_FT_BITMAP
		for(r=0; r < bitmap->rows; r++) {
			int c;
			uchar *span = bitmap->buffer+(r*bitmap->pitch);
			for(c = 0; c < bitmap->width; c++) {
				fprint(1, "%02x", span[c]);
			}
			fprint(1,"\n");
		}
#endif

#ifdef DEBUG_9_BITMAP
		for(r=0; r < mc->r.max.y; r++) {
			int c;
			uchar *span = base+(r*mc->width*sizeof(u32int));
			for(c = 0; c < Dx(mc->r); c++) {
				fprint(1, "%02x", span[c]);
			}
			fprint(1,"\n");
		}
#endif
	}
	fc->x = x;

	// round up to 32-bit boundary
	// so that in-memory data is same
	// layout as in-file data.
	if(antialias)
		x += -x & 3;
	else
		x += -x & 31;
	m1 = allocmemimage(Rect(0, 0, x, y), antialias ? GREY8 : GREY1);
	memimagedraw(m1, m1->r, m, m->r.min, memopaque, ZP, S);
	freememimage(m);

	sf->name = nil;
	sf->n = hi+1 - lo;
	sf->height = Dy(m1->r);
	sf->ascent = Dy(m1->r) - y0;
	sf->info = fc0;
	sf->bits = m1;

	FT_Done_Face(face);
	return sf;
}

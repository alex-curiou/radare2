/* radare - LGPL - Copyright 2009-2018 - pancake */

#include "r_cons.h"
#include "r_core.h"
#include "r_types.h"
#include "r_io.h"

static const char *help_msg_S[] = {
	"Usage:","S[?-.*=adlr] [...]","",
	"S","","list sections",
	"S"," paddr va sz [vsz] name rwx","add new section (if(!vsz)vsz=sz)",
	"S.","","show current section name",
	"S.-*","","remove all sections in current offset",
	"S*","","list sections (in radare commands)",
	"S-[id]","","remove section identified by id",
	"S-.","","remove section at core->offset (can be changed with @)",
	"S=","","list sections (ascii-art bars) (io.va to display paddr or vaddr)",
	NULL
};

static void cmd_section_init(RCore *core) {
	DEFINE_CMD_DESCRIPTOR (core, S);
}

#define PRINT_CURRENT_SEEK \
        if (i > 0 && len != 0) { \
                if (seek == UT64_MAX) seek = 0; \
                io->cb_printf ("=>  0x%08"PFMT64x" |", seek); \
                for (j = 0; j < width; j++) { \
                        io->cb_printf ( \
                                ((j*mul) + min >= seek && \
                                (j*mul) + min <= seek + len) \
                                ? "^" : "-"); \
                } \
                io->cb_printf ("| 0x%08"PFMT64x"\n", seek+len); \
        }

static void list_section_visual(RIO *io, ut64 seek, ut64 len, int use_color, int cols) {
	ut64 mul, min = -1, max = -1;
	SdbListIter *iter;
	RIOSection *s;
	int j, i = 0;
	int width = cols - 70;
	if (width < 1) {
		width = 30;
	}
	// seek = r_io_section_vaddr_to_maddr_try (io, seek);
	// seek = r_io_section_vaddr_to_maddr_try (io, seek);
	ls_foreach (io->sections, iter, s) {
		if (min == -1 || s->paddr < min) {
			min = s->paddr;
		}
		if (max == -1 || s->paddr+s->size > max) {
			max = s->paddr + s->size;
		}
	}
	mul = (max-min) / width;
	if (min != -1 && mul != 0) {
		const char * color = "", *color_end = "";
		char buf[128];
		i = 0;
		ls_foreach (io->sections, iter, s) {
			r_num_units (buf, s->size);
			if (use_color) {
				color_end = Color_RESET;
				if (s->perm & R_PERM_X) { // exec bit
					color = Color_GREEN;
				} else if (s->perm & R_PERM_W) { // write bit
					color = Color_RED;
				} else {
					color = "";
					color_end = "";
				}
			} else {
				color = "";
				color_end = "";
			}
			if (io->va) {
				io->cb_printf ("%02d%c %s0x%08"PFMT64x"%s |", s->id,
						(seek >= s->vaddr && seek < s->vaddr + s->vsize) ? '*' : ' ',
						color, s->vaddr, color_end);
			} else {
				io->cb_printf ("%02d%c %s0x%08"PFMT64x"%s |", s->id,
						(seek >= s->paddr && seek < s->paddr + s->size) ? '*' : ' ',
						color, s->paddr, color_end);
			}
			for (j = 0; j < width; j++) {
				ut64 pos = min + (j * mul);
				ut64 npos = min + ((j + 1) * mul);
				if (s->paddr < npos && (s->paddr + s->size) > pos)
					io->cb_printf ("#");
				else io->cb_printf ("-");
			}
			if (io->va) {
				io->cb_printf ("| %s0x%08"PFMT64x"%s %5s %s  %04s\n",
						color, s->vaddr + s->vsize, color_end, buf,
						r_str_rwx_i (s->perm), s->name);
			} else {
				io->cb_printf ("| %s0x%08"PFMT64x"%s %5s %s  %04s\n",
						color, s->paddr+s->size, color_end, buf,
						r_str_rwx_i (s->perm), s->name);
			}

			i++;
		}
		PRINT_CURRENT_SEEK;
	}
}

static void __section_list (RIO *io, ut64 offset, RPrint *print, int rad) {
	SdbListIter *iter;
	RIOSection *s;

	if (!io || !io->sections || !print || !print->cb_printf) {
		return;
	}
	if (rad == '=') { // "S="
		int cols = r_cons_get_size (NULL);
		list_section_visual (io, offset, -1, print->flags & R_PRINT_FLAGS_COLOR, cols);
	} else if (rad) {
		ls_foreach (io->sections, iter, s) {
			char *n = strdup (s->name);
			r_name_filter (n, strlen (n));
			print->cb_printf ("f section.%s %"PFMT64d" 0x%"PFMT64x"\n", n, s->size, s->vaddr);
			print->cb_printf ("S 0x%08"PFMT64x" 0x%08"PFMT64x" 0x%08"
				PFMT64x" 0x%08"PFMT64x" %s %s\n", s->paddr,
				s->vaddr, s->size, s->vsize, n, r_str_rwx_i (s->perm));
			free (n);
		}
	} else {
		ls_foreach (io->sections, iter, s) {
			print->cb_printf ("[%02d:%02d]", s->bin_id, s->id);
			if (io->va) {
				if ((s->vaddr <= offset) && ((offset - s->vaddr) < s->vsize)) {
					print->cb_printf (" * ");
				} else {
					print->cb_printf (" . ");
				}
			} else {
				if ((s->paddr <= offset) && ((offset - s->paddr) < s->size)) {
					print->cb_printf (" * ");
				} else {
					print->cb_printf (" . ");
				}
			}
			print->cb_printf ("pa=0x%08"PFMT64x" %s va=0x%08"PFMT64x
				" sz=0x%04"PFMT64x" vsz=0x%04"PFMT64x" %s", s->paddr,
				r_str_rwx_i (s->perm), s->vaddr, s->size, s->vsize, s->name);
			if (s->arch && s->bits) {
				print->cb_printf ("  ; %s %d", r_sys_arch_str (s->arch),
					s->bits);
			}
			print->cb_printf ("\n");
		}
	}
}

static int cmd_section(void *data, const char *input) {
	RCore *core = (RCore *)data;
	switch (*input) {
	case '?': // "S?"
		r_core_cmd_help (core, help_msg_S);
		break;
	case '-': // "S-"
		// remove all sections
		if (input[1] == '*') {
			r_io_section_init (core->io);
		}
		if (input[1] == '.') {
			RIOSection *s = r_io_section_vget (core->io, core->offset);
			if (!s) {
				return 0;
			}
			// use offset
			r_io_section_rm (core->io, s->id);
		}
		if (input[1]) {
			r_io_section_rm (core->io, atoi (input + 1));
		}
		break;
	case ' ': // "S "
		switch (input[1]) {
		case '-': // "S -" remove
			if (input[2] == '?' || input[2] == '\0') {
				eprintf ("Usage: S -N   # where N is the "
					" section index\n");
			} else {
				r_io_section_rm (core->io, atoi (input + 1));
			}
			break;
		default:
			{
			int i, rwx = 7;
			char *ptr = strdup (input + 1);
			const char *name = NULL;
			char vname[64];
			ut64 vaddr = 0LL;
			ut64 paddr = 0LL;
			ut64 size = 0LL;
			ut64 vsize = 0LL;
			int bin_id = -1;
			int fd = r_core_file_cur_fd (core);
			i = r_str_word_set0 (ptr);
			switch (i) {
			case 7: //get bin id
				bin_id = r_num_math (core->num, r_str_word_get0 (ptr, 6));
			case 6: // get rwx
				rwx = r_str_rwx (r_str_word_get0 (ptr, 5));
			case 5: // get name
				name = r_str_word_get0 (ptr, 4);
			case 4: // get vsize
				vsize = r_num_math (core->num, r_str_word_get0 (ptr, 3));
			case 3: // get size
				size = r_num_math (core->num, r_str_word_get0 (ptr, 2));
			case 2: // get vaddr
				vaddr = r_num_math (core->num, r_str_word_get0 (ptr, 1));
			case 1: // get paddr
				paddr = r_num_math (core->num, r_str_word_get0 (ptr, 0));
			}
			if (!vsize) {
				vsize = size;
				if (i > 3) {
					name = r_str_word_get0 (ptr, 3);
				}
				if (i > 4) {
					rwx = r_str_rwx (r_str_word_get0 (ptr, 4));
				}
			}
			if (!name || !*name) {
				sprintf (vname, "area%d", (int)ls_length (core->io->sections));
				name = vname;
			}
			RIOSection *sec = r_io_section_add (core->io, paddr, vaddr, size, vsize, rwx, name, bin_id, fd);
			r_io_create_mem_for_section (core->io, sec);
			free (ptr);
			}
			break;
		}
		break;
	case '.': // "S."
		if (input[1] == '-') { // "S.-"
			ut64 o = core->offset;
			SdbListIter *iter, *iter2;
			RIOSection *s;
			if (core->io->va || core->io->debug) {
				s = r_io_section_vget (core->io, o); 
				o = s ? o - s->vaddr + s->paddr : o;
			}
			ls_foreach_safe (core->io->sections, iter, iter2, s) {
				if (o >= s->paddr && o < s->paddr + s->size) {
					r_io_section_rm (core->io, s->id);
					if (input[2] != '*') {
						break;
					}
				}
			}
		} else {
			ut64 o = core->offset;
			SdbListIter *iter;
			RIOSection *s;
			if (core->io->va || core->io->debug) {
				s = r_io_section_vget (core->io, o); 
				o = s ? o - s->vaddr + s->paddr : o;
			}
			if (input[1] == 'j') { // "S.j"
				r_cons_printf ("[");
				ls_foreach (core->io->sections, iter, s) {
					if (o >= s->paddr && o < s->paddr + s->size) {
						char *name = r_str_escape (s->name);
						r_cons_printf ("{\"start\":%" PFMT64u ",\"end\":%" PFMT64u ",\"name\":\"%s\"}",
							s->paddr + s->vaddr,
							s->paddr + s->vaddr + s->size,
							name);
						free (name);
					}
				}
				r_cons_printf ("]");
			} else {
				ls_foreach (core->io->sections, iter, s) {
					if (o >= s->paddr && o < s->paddr + s->size) {
						r_cons_printf ("0x%08"PFMT64x" 0x%08"PFMT64x" %s\n",
							s->paddr + s->vaddr,
							s->paddr + s->vaddr + s->size,
							s->name);
						break;
					}
				}
			}
		}
		break;
	case '\0': // "S"
	case '=': // "S="
	case '*': // "S*"
		__section_list (core->io, core->offset, core->print, *input);
		break;
	}
	return 0;
}
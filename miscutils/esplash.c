/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2008 Mikhail Gusarov <dottedmag@dottedmag.net>
 *
 * Based on fbsplash.c by Michele Sanges <michele.sanges@otomelara.it>,
 * <michele.sanges@gmail.it>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this tarball for details.
 *
 * Usage:
 * - put somewhere esplash.cfg file and an image (fb dump, possibly compressed).
 * - run applet: $ setsid esplash [params] &
 * - commands for fifo:
 *   "NN" (ASCII decimal number) - percentage to show on progress bar.
 *   "exit" (or just close fifo) - well you guessed it.
 */

#include "libbb.h"
#include <linux/fb.h>

#define CONFIG_OPTIONS 6

struct globals {
	unsigned char *addr;	// pointer to framebuffer memory
	unsigned ns[CONFIG_OPTIONS];	// n-parameters
	unsigned last_percent;	// boot percent displayed last time
	const char *image_filename;
	const char *fb_device;
	struct fb_var_screeninfo scr_var;
	struct fb_fix_screeninfo scr_fix;
};
#define G (*ptr_to_globals)
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
} while (0)

#define nbar_width	ns[0]	// progress bar width
#define nbar_height	ns[1]	// progress bar height
#define nbar_posx	ns[2]	// progress bar horizontal position
#define nbar_posy	ns[3]	// progress bar vertical position
#define nbar_col	ns[4]	// progress bar color
#define screen_orientation	ns[5]	// screen orientation

/**
 * Draw filled rectangle on framebuffer
 * \param from_x,from_y upper left position
 * \param to_x,to_y down right position, not including
 * \param n color
 */
static void fb_drawfullrectangle(int from_x, int from_y, int to_x, int to_y,
	unsigned char n)
{
	int cnt1, cnt2, nypos;
	unsigned char *ptr;

	cnt1 = to_y - from_y;
	nypos = from_y;
	do {
		ptr = G.addr + (nypos * G.scr_var.xres + from_x);
		cnt2 = to_x - from_x;
		do {
			*ptr++ = n;
		} while (--cnt2 > 0);

		nypos++;
	} while (--cnt1 > 0);
}


/**
 *	Draw a progress bar on framebuffer
 * \param percent percentage of loading
 */
static void fb_drawprogressbar(unsigned percent)
{
	int x, x2;

	// [from_x, to_x)
	int from_x = G.nbar_posx + G.nbar_width * G.last_percent / 100;
	int to_x = G.nbar_posx + G.nbar_width * percent / 100;

	// [from_y, to_y)
	int from_y = G.nbar_posy;
	int to_y = G.nbar_posy + G.nbar_height;

	if(percent <= G.last_percent)
		return;

	switch(G.screen_orientation) {
		case 90:
			// not implemented
			break;
		case 180:
			// not implemented
			break;
		case 270:
			x = from_x;
			x2 = to_x;
			from_x = G.scr_var.xres - to_y;
			to_x = G.scr_var.xres - from_y;
			from_y = x;
			to_y = x2;
			break;
	}

	fb_drawfullrectangle(from_x, from_y, to_x, to_y, G.nbar_col);

	G.last_percent = percent;
}

/**
 *	Open and initialize the framebuffer device
 * \param *strfb_device pointer to framebuffer device
 */
static bool fb_open(const char *strfb_device)
{
	int fb_size;

	int fbfd = xopen(strfb_device, O_RDWR);

	if(fbfd == -1)
		return false;

	// framebuffer properties
	xioctl(fbfd, FBIOGET_VSCREENINFO, &G.scr_var);
	xioctl(fbfd, FBIOGET_FSCREENINFO, &G.scr_fix);
	fb_size = G.scr_var.xres * G.scr_var.yres;

	if (G.scr_var.bits_per_pixel != 8)
		bb_error_msg_and_die("only 8 bpp is supported");

	// map the device in memory
	G.addr = mmap(NULL,
			fb_size,
			PROT_WRITE, MAP_SHARED, fbfd, 0);
	if (G.addr == MAP_FAILED)
		bb_perror_msg_and_die("can't mmap %s", strfb_device);
	close(fbfd);

    if(G.image_filename) {
        char *p;
        int cnt;
        int themefd = open_zipped(G.image_filename);
        char *buffer = mmap(NULL, fb_size,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANON,
                      /* ignored: */ -1, 0);
        if (buffer == MAP_FAILED)
            buffer = alloca(fb_size);

        p = buffer;
        cnt = 0;
        while(1) {
            ssize_t rd;

            rd = safe_read(themefd, p, (fb_size - cnt) > 4096 ? 4096 : (fb_size - cnt));

            if(rd > 0) {
                p += rd;
                cnt += rd;
            }

            if(fb_size == cnt) {
                memcpy(G.addr, buffer, fb_size);

                p = buffer;
                cnt = 0;
            }

            if(rd <= 0)
                break;
        }

        munmap(buffer, fb_size);
        close(themefd);
    }

    return true;
}

/**
 *	Parse configuration file
 * \param *cfg_filename name of the configuration file
 */
static void init(const char *cfg_filename)
{
	static const char const param_names[] ALIGN1 =
		"BAR_WIDTH\0" "BAR_HEIGHT\0"
		"BAR_LEFT\0" "BAR_TOP\0"
		"BAR_COLOR\0" "SCREEN_ORIENTATION\0"
		"IMAGE\0" "FB\0"
		;
	char *token[2];
	parser_t *parser = config_open2(cfg_filename, xfopen_stdin);
	while (config_read(parser, token, 2, 2, "#=",
				    (PARSE_NORMAL | PARSE_MIN_DIE) & ~(PARSE_TRIM | PARSE_COLLAPSE))) {
		int i = index_in_strings(param_names, token[0]);
		if (i < 0)
			bb_error_msg_and_die("syntax error: '%s'", token[0]);
		if (i >= 0 && i < CONFIG_OPTIONS) {
			unsigned val = xatoi_u(token[1]);
			G.ns[i] = val;
		} else if(i == 6) {
			if(G.image_filename == NULL || G.image_filename[0] == '\0')
				G.image_filename = strdup(token[1]);
		} else if(i == 7) {
			if(G.fb_device == NULL || G.fb_device[0] == '\0')
				G.fb_device = strdup(token[1]);
		}
	}
	config_close(parser);
}


int esplash_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int esplash_main(int argc UNUSED_PARAM, char **argv)
{
	const char *cfg_filename, *fifo_filename;
	FILE *fp = fp; // for compiler
	char *num_buf;
	unsigned num;
	bool bCursorOff, background;
	int opts;

	INIT_G();

	// parse command line options
	cfg_filename = NULL;
	fifo_filename = NULL;

	opts = getopt32(argv, "bcs:d:i:f:",
	                &G.image_filename, &G.fb_device, &cfg_filename, &fifo_filename);
	background = opts & 1;
	bCursorOff = opts & 2;

	// parse configuration file
	if (cfg_filename)
		init(cfg_filename);

	if (fifo_filename && bCursorOff) {
		// hide cursor (BEFORE any fb ops)
		full_write(STDOUT_FILENO, "\x1b" "[?25l", 6);
	}

	if(G.fb_device == NULL || G.fb_device[0] == '\0')
		G.fb_device = "/dev/fb0";

	if(!fb_open(G.fb_device))
		return EXIT_FAILURE;

	if (!fifo_filename)
		return EXIT_SUCCESS;

    if(background)
        bb_daemonize(DAEMON_CHDIR_ROOT | DAEMON_DEVNULL_STDIO);

	fp = xfopen_stdin(fifo_filename);
	if (fp != stdin) {
		// For named pipes, we want to support this:
		//  mkfifo cmd_pipe
		//  esplash -f cmd_pipe .... &
		//  ...
		//  echo 33 >cmd_pipe
		//  ...
		//  echo 66 >cmd_pipe
		// This means that we don't want esplash to get EOF
		// when last writer closes input end.
		// The simplest way is to open fifo for writing too
		// and become an additional writer :)
		open(fifo_filename, O_WRONLY); // errors are ignored
	}

	fb_drawprogressbar(0);
	// Block on read, waiting for some input.
	// Use of <stdio.h> style I/O allows to correctly
	// handle a case when we have many buffered lines
	// already in the pipe
	while ((num_buf = xmalloc_fgetline(fp)) != NULL) {
		if (strncmp(num_buf, "exit", 4) == 0) {
			break;
		}
		num = atoi(num_buf);
		if (isdigit(num_buf[0]) && (num <= 100)) {
			fb_drawprogressbar(num);
		}
		free(num_buf);
	}

	if (bCursorOff) // restore cursor
		full_write(STDOUT_FILENO, "\x1b" "[?25h", 6);

	return EXIT_SUCCESS;
}

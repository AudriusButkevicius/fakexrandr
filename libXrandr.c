#include <dlfcn.h>
#include <stdio.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

/*
	We use this XID modifier to flag outputs and CRTCs as
	fake by XORing with it.

	On the choice: A typical XID is of the form
	 client_id | (xid_mask & arbitrary value),
	according to the documentation of the X-Resource extension. On my system,
	 xid_mask = 0x001FFFFF
	and client_id == 0 for all reources mentioned in the RandR protocol.
	All we need to do is to choose XID_SPLIT_MASK such that
	XID_SPLIT_MASK & xid_mask == 0.
*/
#define XID_SPLIT_SHIFT 21
#define XID_SPLIT_MASK  0x7FE00000

/*
	Generated by ./configure:
*/
#include "config.h"

/*
	The skeleton file is created by ./make_skeleton.py

	It contains wrappers around all Xrandr functions which are not
	explicitly defined in this C file, replacing all references to
	crtcs and outputs which are fake with the real ones.
*/
#include "skeleton.h"

/*
	To avoid memory leaks, we simply use two big buffers to
	store the fake data
*/
static char fake_output_buffer[sizeof(RRCrtc) * 255];
static char fake_crtc_buffer  [sizeof(RRCrtc) * 255];
static char fake_output_name_buffer[255 * 255];

static void _init() __attribute__((constructor));
static void _init() {
	void *xrandr_lib = dlopen(REAL_XRANDR_LIB, RTLD_LAZY | RTLD_GLOBAL);

	/*
		The following macro is defined by the skeleton header. It initializes
		static variables called _XRRfn with references to the real XRRfn
		functions.
	*/
	FUNCTION_POINTER_INITIALIZATIONS;
}

/*
	Helper functions to generate the fake data
*/
static bool check_if_crtc_is_wrong(Display *dpy, XRRScreenResources *resources, RRCrtc crtc) {
	XRRCrtcInfo *info = _XRRGetCrtcInfo(dpy, resources, crtc & ~XID_SPLIT_MASK);
	bool retval = info->width == SPLIT_SCREEN_WIDTH && info->height == SPLIT_SCREEN_HEIGHT;
	Xfree(info);
	return retval;
}

static bool check_if_output_is_wrong(Display *dpy, XRRScreenResources *resources, RROutput output) {
	XRROutputInfo *info = _XRRGetOutputInfo(dpy, resources, output & ~XID_SPLIT_MASK);
	bool retval = info->crtc != 0 && check_if_crtc_is_wrong(dpy, resources, info->crtc);
	Xfree(info);
	return retval;
}

static void append_fake_crtc(int *count, RRCrtc **crtcs, RRCrtc real_crtc, int append) {
	assert((real_crtc & XID_SPLIT_MASK) == 0L);
	assert(sizeof(RRCrtc) * (*count + append) < sizeof(fake_crtc_buffer));

	RRCrtc *new_space = (RRCrtc *)fake_crtc_buffer;
	memcpy(fake_crtc_buffer, *crtcs, sizeof(RRCrtc) * (*count));

	int index = (*count);
	int i;
	for(i = 1; i <= append; ++i, ++index) {
		memcpy(&fake_crtc_buffer[sizeof(RRCrtc) * index], *crtcs, sizeof(RRCrtc));
		new_space[index] = real_crtc | (i << XID_SPLIT_SHIFT);
	}

	(*count) += append;
	*crtcs    = new_space;
}

static void append_fake_output(int *count, RROutput **outputs, RROutput real_output, int append) {
	assert((real_output & XID_SPLIT_MASK) == 0L);
	assert(sizeof(RROutput) * (*count + append) < sizeof(fake_output_buffer));

	RRCrtc *new_space = (RRCrtc *)fake_output_buffer;
	memcpy(fake_output_buffer, *outputs, sizeof(RROutput) * (*count));

	int index = (*count);
	int i;
	for(i = 1; i <= append; ++i, ++index) {
		memcpy(&fake_output_buffer[sizeof(RROutput) * index], *outputs, sizeof(RROutput));
		new_space[index] = real_output | (i << XID_SPLIT_SHIFT);
	}

	(*count) += append;
	*outputs  = new_space;
}

static XRRScreenResources *augment_resources(Display *dpy, Window window, XRRScreenResources *retval) {
	int i;
	for(i = 0; i < retval->ncrtc; i++) {
		if(check_if_crtc_is_wrong(dpy, retval, retval->crtcs[i])) {
			/* Add a second crtc (becomes the virtual split screen) */
			append_fake_crtc(&retval->ncrtc, &retval->crtcs, retval->crtcs[i], EXTRA_SCREENS);
			break;
		}
	}
	for(i = 0; i < retval->noutput; i++) {
		if(check_if_output_is_wrong(dpy, retval, retval->outputs[i])) {
			/* Add a second output (becomes the virtual split screen) */
			append_fake_output(&retval->noutput, &retval->outputs, retval->outputs[i], EXTRA_SCREENS);
			break;
		}
	}

	/*
		Find and correct the mode info, this is purely for completeness, the
		virtual display areas already work as intended, but this corrects the
		output shown by 'xrandr' for individual screen sizes.
	*/
	for(i = 0; i < retval->nmode; i++) {
		XRRModeInfo *mi = &retval->modes[i];
		if (mi->width == SPLIT_SCREEN_WIDTH && mi->height == SPLIT_SCREEN_HEIGHT) {
			mi->width      /= (EXTRA_SCREENS + 1);
			mi->hSyncStart /= (EXTRA_SCREENS + 1);
			mi->hSyncEnd   /= (EXTRA_SCREENS + 1);
			mi->hTotal     /= (EXTRA_SCREENS + 1);
			mi->dotClock   /= (EXTRA_SCREENS + 1);

			/*
				There will always be enough room to write the new name as the number will
				always be lower then the original, and thus equal or shorter in length.
			*/
			snprintf(mi->name, mi->nameLength + 1, "%ux%u", mi->width, mi->height);
			break;
		}
	}

	return retval;
}

/*
	Overridden library functions to add the fake output
*/

XRRScreenResources *XRRGetScreenResources(Display *dpy, Window window) {
	XRRScreenResources *retval = augment_resources(dpy, window, _XRRGetScreenResources(dpy, window));
	return retval;
}

XRRScreenResources *XRRGetScreenResourcesCurrent(Display *dpy, Window window) {
	XRRScreenResources *retval = augment_resources(dpy, window, _XRRGetScreenResourcesCurrent(dpy, window));
	return retval;
}

XRROutputInfo *XRRGetOutputInfo(Display *dpy, XRRScreenResources *resources, RROutput output) {
	XRROutputInfo *retval = _XRRGetOutputInfo(dpy, resources, output & ~XID_SPLIT_MASK);

	if(check_if_output_is_wrong(dpy, resources, output)) {
		retval->mm_width /= (EXTRA_SCREENS + 1);
		if(output & XID_SPLIT_MASK) {
			int fake_number = (output >> XID_SPLIT_SHIFT);
			char *output_name = fake_output_buffer + fake_number * 255;
			snprintf(output_name, 255, "%.200s~%d", retval->name, fake_number);
			retval->name = output_name;
			append_fake_crtc(&retval->ncrtc, &retval->crtcs, retval->crtc, EXTRA_SCREENS);
			retval->crtc = retval->crtc | (output & XID_SPLIT_MASK);
		}
	}

	return retval;
}

XRRCrtcInfo *XRRGetCrtcInfo(Display *dpy, XRRScreenResources *resources, RRCrtc crtc) {
	XRRCrtcInfo *retval = _XRRGetCrtcInfo(dpy, resources, crtc & ~XID_SPLIT_MASK);

	if(check_if_crtc_is_wrong(dpy, resources, crtc)) {
		retval->width /= (EXTRA_SCREENS + 1);
		if (crtc & XID_SPLIT_MASK)
			retval->x = ((crtc >> XID_SPLIT_SHIFT) - 1) * retval->width;
		else	retval->x = 0;

		if(crtc & XID_SPLIT_MASK) {
			retval->x += retval->width;
		}
	}

	return retval;
}

int XRRSetCrtcConfig(Display *dpy, XRRScreenResources *resources, RRCrtc crtc, Time timestamp, int x, int y, RRMode mode, Rotation rotation, RROutput *outputs, int noutputs) {
	if(crtc & XID_SPLIT_MASK) {
		return 0;
	}
	int i;
	for(i=0; i<noutputs; i++) {
		if(outputs[i] & XID_SPLIT_MASK) {
			return 0;
		}
	}

	return _XRRSetCrtcConfig(dpy, resources, crtc, timestamp, x, y, mode, rotation, outputs, noutputs);
}

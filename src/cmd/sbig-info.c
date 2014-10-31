/*****************************************************************************\
 *  Copyright (c) 2014 Jim Garlick All rights reserved.
 *
 *  This file is part of the sbig-util.
 *  For details, see https://github.com/garlick/sbig-util.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 3 of the license, or (at your option)
 *  any later version.
 *
 *  sbig-util is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <libgen.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <getopt.h>

#include "src/common/libsbig/sbig.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/bcd.h"
#include "src/common/libini/ini.h"

typedef struct {
    double focal_length;
} opt_t;

void show_cfw_info (const char *sbig_udrv, const char *sbig_device,
                    int ac, char **av);
void show_driver_info (const char *sbig_udrv, int ac, char **av);
void show_ccd_info (const char *sbig_udrv, const char *sbig_device,
                    int ac, char **av);
void show_cooler_info (const char *sbig_udrv, const char *sbig_device,
                       int ac, char **av);
void show_fov (const char *sbig_udrv, const char *sbig_device, opt_t opt,
               int ac, char **av);
int config_cb (void *user, const char *section, const char *name,
               const char *value);

#define OPTIONS "h"
static const struct option longopts[] = {
    {"help",          no_argument,           0, 'h'},
    {0, 0, 0, 0},
};

void usage (void)
{
    fprintf (stderr,
"Usage: sbig-info driver\n"
"       sbig-info ccd {tracking|imaging}\n"
"       sbig-info cfw\n"
"       sbig-info cooler\n"
"       sbig-info fov {tracking|imaging} [focal-length]\n"
);
    exit (1);
}

int main (int argc, char *argv[])
{
    const char *sbig_udrv = getenv ("SBIG_UDRV");
    const char *sbig_device = getenv ("SBIG_DEVICE");
    const char *config_filename = getenv ("SBIG_CONFIG_FILE");
    int ch;
    char *cmd;
    opt_t opt;

    log_init ("sbig-info");

    memset (&opt, 0, sizeof (opt));

    while ((ch = getopt_long (argc, argv, OPTIONS, longopts, NULL)) != -1) {
        switch (ch) {
            case 'h': /* --help */
            default:
                usage ();
        }
    }
    if (optind == argc)
        usage ();
    cmd = argv[optind++];

    if (!sbig_udrv)
        msg_exit ("SBIG_UDRV is not set");
    if (!sbig_device)
        msg_exit ("SBIG_DEVICE is not set");
    if (!config_filename)
        msg_exit ("SBIG_CONFIG_FILE is not set");
    (void)ini_parse (config_filename, config_cb, &opt);

    if (!strcmp (cmd, "ccd")) {
        show_ccd_info (sbig_udrv, sbig_device, argc - optind, argv + optind);
    } else if (!strcmp (cmd, "driver")) {
        show_driver_info (sbig_udrv, argc - optind, argv + optind);
    } else if (!strcmp (cmd, "cfw")) {
        show_cfw_info (sbig_udrv, sbig_device, argc - optind, argv + optind);
    } else if (!strcmp (cmd, "cooler")) {
        show_cooler_info(sbig_udrv, sbig_device, argc - optind, argv + optind);;
    } else if (!strcmp (cmd, "fov")) {
        show_fov (sbig_udrv, sbig_device, opt, argc - optind, argv + optind);
    } else
        usage ();

    log_fini ();
    return 0;
}

int config_cb (void *user, const char *section, const char *name,
               const char *value)
{
    opt_t *opt = user;
    if (!strcmp (section, "config")) {
        if (!strcmp (name, "focal_length"))
            opt->focal_length = strtod (value, NULL);
    }
    return 0;
}

sbig_t init_driver (const char *sbig_udrv)
{
    int e;
    sbig_t sb;

    if (!(sb = sbig_new ()))
        err_exit ("sbig_new");
    if (sbig_dlopen (sb, sbig_udrv) != 0)
        msg_exit ("%s", dlerror ());
    if ((e = sbig_open_driver (sb)) != 0)
        msg_exit ("sbig_open_driver: %s", sbig_get_error_string (sb, e));
    return sb;
}

void fini_driver (sbig_t sb)
{
    sbig_destroy (sb);
}

void init_device (sbig_t sb, const char *sbig_device)
{
    SBIG_DEVICE_TYPE device = sbig_devstr (sbig_device);
    CAMERA_TYPE type;
    int e;

    if ((e = sbig_open_device (sb, device)) != 0)
        msg_exit ("sbig_open_device: %s", sbig_get_error_string (sb, e));
    if ((e = sbig_establish_link (sb, &type)) != 0)
        msg_exit ("sbig_establish_link: %s", sbig_get_error_string (sb, e));
}

void fini_device (sbig_t sb)
{
    int e;
    if ((e = sbig_close_device (sb)) != 0)
        msg_exit ("sbig_close_device: %s", sbig_get_error_string (sb, e));
}

static int lookup_readoutmode_index (GetCCDInfoResults0 info,
                                     READOUT_BINNING_MODE mode)
{
    int i;
    for (i = 0; i < info.readoutModes; i++) {
        if (info.readoutInfo[i].mode == mode)
            return i;
    }
    return -1;
}

void show_fov (const char *sbig_udrv, const char *sbig_device, opt_t opt,
               int ac, char **av)
{
    sbig_t sb;
    double fov_height, fov_width; /* field of view in arcseconds */
    double sensor_width, sensor_height; /* sensor dims in mm */
    double focal_length;
    int e, rm_index;
    sbig_ccd_t ccd;
    CCD_REQUEST chip;
    READOUT_BINNING_MODE readout_mode = RM_1X1;
    GetCCDInfoResults0 info;

    if (ac != 1 && ac != 2)
        usage ();
    if (!strcmp (av[0], "tracking"))
        chip = CCD_TRACKING;
    else if (!strcmp (av[0], "imaging"))
        chip = CCD_IMAGING;
    else
        usage ();
    if (ac == 2)
        focal_length = strtod (av[1], NULL);
    else {    
        if (opt.focal_length == 0)
            msg_exit("Please set focal_length");
        focal_length = opt.focal_length;
    }
    msg ("focal length: %.2fmm", focal_length);

    sb = init_driver (sbig_udrv);
    init_device (sb, sbig_device);

    if ((e = sbig_ccd_create (sb, chip, &ccd)))
        msg_exit ("sbig_ccd_create: %s", sbig_get_error_string (sb, e));
    if ((e = sbig_ccd_get_info0 (ccd, &info)) != 0)
        msg_exit ("sbig_ccd_get_info: %s", sbig_get_error_string (sb, e));

    rm_index = lookup_readoutmode_index (info, readout_mode);
    if (rm_index < 0)
        msg_exit ("could not look up readout mode!");

    sensor_height = 1E-3 * info.readoutInfo[rm_index].height
                  * bcd6_2 (info.readoutInfo[rm_index].pixelHeight);
    sensor_width = 1E-3 * info.readoutInfo[rm_index].width
                 * bcd6_2 (info.readoutInfo[rm_index].pixelWidth);
    msg ("sensor: %.2fmm H x %.2fmm W",
         sensor_height, sensor_width);

    fov_height = 3478 * (sensor_height / focal_length);
    fov_width = 3478 * (sensor_width / focal_length);

    msg ("field of view: %.2f'H x %.2f'W", fov_height, fov_width);
    if (fov_height > 60)
        msg ("           or: %.2f x %.2f degrees", fov_height/60, fov_width/60);

    sbig_ccd_destroy (ccd);
    fini_device (sb);
    fini_driver (sb);
}

void show_cooler_info (const char *sbig_udrv, const char *sbig_device,
                       int ac, char **av)
{
    sbig_t sb;
    QueryTemperatureStatusResults2 info;
    int e;

    sb = init_driver (sbig_udrv);
    init_device (sb, sbig_device);

    if ((e = sbig_temp_get_info (sb, &info)) != CE_NO_ERROR)
        msg_exit ("sbig_temp_get_info: %s", sbig_get_error_string (sb, e));
    msg ("cooling:              %s", info.coolingEnabled ? "enabled"
                                                         : "disabled");
    msg ("imaging ccd setpoint: %.2fC", info.ccdSetpoint);
    msg ("imaging ccd:          %.2fC", info.imagingCCDTemperature);
    msg ("tracking setpoint:    %.2fC", info.trackingCCDSetpoint);
    msg ("tracking ccd:         %.2fC", info.trackingCCDTemperature);
    //msg ("ext-track ccd:     %.2fC", info.externalTrackingCCDTemperature);
    msg ("ambient:              %.2fC", info.ambientTemperature);
    msg ("heatsink:             %.2fC", info.heatsinkTemperature);

    msg ("imaging pwr:          %.0f%%", info.imagingCCDPower);
    msg ("tracking pwr:         %.0f%%", info.trackingCCDPower);
    //msg ("ext-track pwr:        %.0f percent", info.externalTrackingCCDPower);
    
    msg ("fan:                  %s", info.fanEnabled == FS_OFF ? "off"
                                   : info.fanEnabled == FS_ON ? "manual"
                                   : "auto");
    msg ("fan pwr:              %.0f%%", info.fanPower);
    msg ("fan speed:            %.0fRPM", info.fanSpeed);

    fini_device (sb);
    fini_driver (sb);
}

void show_driver_info (const char *sbig_udrv, int ac, char **av)
{
    GetDriverInfoResults0 info;
    int e;
    char version[16];
    sbig_t sb;

    if (ac != 0)
        msg_exit ("driver takes no arguments");

    sb = init_driver (sbig_udrv);

    if ((e = sbig_get_driver_info (sb, DRIVER_STD, &info)) != 0)
        msg_exit ("sbig_get_driver_info: %s", sbig_get_error_string (sb, e));
    bcd4str (info.version, version, sizeof (version));
    msg ("version: %s", version);
    msg ("name:    %s", info.name);
    msg ("maxreq:  %d", info.maxRequest);

    fini_driver (sb);
}

void show_cfw_info (const char *sbig_udrv, const char *sbig_device,
                    int ac, char **av)
{
    int e;
    ulong fwrev, numpos;
    CFW_MODEL_SELECT model;
    sbig_t sb;

    if (ac != 0)
        msg_exit ("cfw takes no arguments");

    sb = init_driver (sbig_udrv);
    init_device (sb, sbig_device);

    if ((e = sbig_cfw_get_info (sb, &model, &fwrev, &numpos)) != 0)
        msg_exit ("sbig_cfw_get_info: %s", sbig_get_error_string (sb, e));
    msg ("model:            %s", sbig_strcfw (model));
    msg ("firmware-version: %lu", fwrev);
    msg ("num-positions:    %lu", numpos);

    fini_device (sb);
    fini_driver (sb);
}

void show_ccd_info (const char *sbig_udrv, const char *sbig_device,
                    int ac, char **av)
{
    int i, e;
    GetCCDInfoResults0 info;
    char version[16];
    sbig_ccd_t ccd;
    sbig_t sb;
    CCD_REQUEST chip;

    if (ac != 1)
        usage ();
    if (!strcmp (av[0], "tracking"))
        chip = CCD_TRACKING;
    else if (!strcmp (av[0], "imaging"))
        chip = CCD_IMAGING;
    else
        usage ();

    sb = init_driver (sbig_udrv);
    init_device (sb, sbig_device);

    if ((e = sbig_ccd_create (sb, chip, &ccd)))
        msg_exit ("sbig_ccd_create: %s", sbig_get_error_string (sb, e));
    if ((e = sbig_ccd_get_info0 (ccd, &info)) != 0)
        msg_exit ("sbig_ccd_get_info: %s", sbig_get_error_string (sb, e));

    /* FIXME: ST5C/237/237A (PixCel 255/237) only support req 0,3,4,5
     * We are making requests 0,1,2,4,5 for ST-7/8/etc
     * Req 6 is STX/STXL specific - but supported by all CCD's.
     */

    bcd4str (info.firmwareVersion, version, sizeof (version));
    msg ("firmware-version: %s", version);
    msg ("camera-type:      %s", sbig_strcam (info.cameraType));
    msg ("name:             %s", info.name);
    msg ("readout-modes:");
    for (i = 0; i < info.readoutModes; i++) {
        msg ("%2d: %4d x %-4d %2.2f e-/ADU %3.2f x %-3.2f microns",
             info.readoutInfo[i].mode,
             info.readoutInfo[i].width,
             info.readoutInfo[i].height,
             bcd2_2 (info.readoutInfo[i].gain),
             bcd6_2 (info.readoutInfo[i].pixelWidth),
             bcd6_2 (info.readoutInfo[i].pixelHeight));
    }
    if (chip == CCD_IMAGING) {
        GetCCDInfoResults2 xinfo;
        if ((e = sbig_ccd_get_info2 (ccd, &xinfo)) != 0)
            msg_exit ("sbig_get_ccd_xinfo: %s", sbig_get_error_string (sb, e));
        msg ("bad columns:       %d", xinfo.badColumns);
        msg ("ABG:               %s", xinfo.imagingABG == ABG_PRESENT ? "yes"
                                                                      : "no");
        msg ("serial-number:     %s", xinfo.serialNumber);
    }
    if (chip == CCD_IMAGING) {
        GetCCDInfoResults4 xinfo;
        ushort cap;
        if ((e = sbig_ccd_get_info4 (ccd, &xinfo)) != 0)
            msg_exit ("sbig_get_ccd_xinfo2: %s", sbig_get_error_string (sb, e));
        cap = xinfo.capabilitiesBits;
        msg ("ccd-type:          %s", (cap & CB_CCD_TYPE_FRAME_TRANSFER)
                                      ? "frame_transfer" : "full frame");
        msg ("electronic-shutter:%s", !(cap & CB_CCD_ESHUTTER_YES) ? "no"
             : "Interline Imaging CCD with electronic shutter and ms exposure");
        msg ("remote-guide-port: %s", (cap & CB_CCD_EXT_TRACKER_YES)
                                      ? "yes" : "no");
        msg ("biorad-tdi-mode:   %s", (cap & CB_CCD_BTDI_YES) ? "yes" : "no");
        msg ("AO8-detected:      %s", (cap & CB_AO8_YES) ? "yes" : "no");
        msg ("frame-buffer:      %s", (cap & CB_FRAME_BUFFER_YES)
                                      ? "yes" : "no");
        msg ("use-startexp2:     %s", (cap & CB_REQUIRES_STARTEXP2_YES)
                                      ? "yes" : "no");
    }
    sbig_ccd_destroy (ccd);
    fini_device (sb);
    fini_driver (sb);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

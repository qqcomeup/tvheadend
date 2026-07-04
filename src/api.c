/*
 *  API - Common functions for control/query API
 *
 *  Copyright (C) 2013 Adam Sutton
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "tvheadend.h"
#include "api.h"
#include "access.h"
#include "clock.h"

#include <inttypes.h>
#include <stdio.h>
#include <sys/statvfs.h>
#include <string.h>
#include <signal.h>

extern void doexit(int x);

typedef struct tvh_sys_cpu_sample {
  uint64_t total;
  uint64_t idle;
} tvh_sys_cpu_sample_t;

typedef struct tvh_sys_net_sample {
  uint64_t rx_bytes;
  uint64_t tx_bytes;
  int64_t time;
} tvh_sys_net_sample_t;

typedef struct api_link {
  const api_hook_t   *hook;
  RB_ENTRY(api_link)  link;
} api_link_t;

RB_HEAD(,api_link) api_hook_tree;
SKEL_DECLARE(api_skel, api_link_t);

static tvh_sys_cpu_sample_t api_serverinfo_prev_cpu;
static tvh_sys_net_sample_t api_serverinfo_prev_net;

static int
api_read_cpu_sample(tvh_sys_cpu_sample_t *sample)
{
  FILE *fp;
  char label[16];
  uint64_t user, nice, system, idle, iowait, irq, softirq, steal;

  fp = fopen("/proc/stat", "r");
  if (!fp)
    return -1;
  if (fscanf(fp, "%15s %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64,
             label, &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) != 9) {
    fclose(fp);
    return -1;
  }
  fclose(fp);
  if (strcmp(label, "cpu"))
    return -1;
  sample->idle = idle + iowait;
  sample->total = user + nice + system + idle + iowait + irq + softirq + steal;
  return 0;
}

static void
api_serverinfo_add_cpu(htsmsg_t *system)
{
  tvh_sys_cpu_sample_t sample;
  uint64_t total_delta, idle_delta;
  double used_percent;

  if (api_read_cpu_sample(&sample))
    return;
  if (api_serverinfo_prev_cpu.total > 0 && sample.total > api_serverinfo_prev_cpu.total) {
    total_delta = sample.total - api_serverinfo_prev_cpu.total;
    idle_delta = sample.idle >= api_serverinfo_prev_cpu.idle ? sample.idle - api_serverinfo_prev_cpu.idle : 0;
    if (total_delta > 0) {
      used_percent = ((double)(total_delta - MIN(idle_delta, total_delta)) * 100.0) / (double)total_delta;
      htsmsg_add_dbl(system, "cpu_percent", used_percent);
    }
  }
  api_serverinfo_prev_cpu = sample;
}

static void
api_serverinfo_add_memory(htsmsg_t *system)
{
  FILE *fp;
  char key[64];
  char unit[16];
  uint64_t value;
  uint64_t total = 0, available = 0;

  fp = fopen("/proc/meminfo", "r");
  if (!fp)
    return;
  while (fscanf(fp, "%63s %"SCNu64" %15s", key, &value, unit) == 3) {
    if (!strcmp(key, "MemTotal:"))
      total = value * 1024;
    else if (!strcmp(key, "MemAvailable:"))
      available = value * 1024;
    if (total && available)
      break;
  }
  fclose(fp);
  if (!total)
    return;
  htsmsg_add_s64(system, "memory_total", total);
  htsmsg_add_s64(system, "memory_available", available);
  htsmsg_add_s64(system, "memory_used", total > available ? total - available : 0);
  htsmsg_add_dbl(system, "memory_used_percent",
                 total > available ? ((double)(total - available) * 100.0) / (double)total : 0.0);
}

static int
api_read_net_sample(tvh_sys_net_sample_t *sample)
{
  FILE *fp;
  char line[512], iface[64];
  uint64_t rx, rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
  uint64_t tx, tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compressed;

  memset(sample, 0, sizeof(*sample));
  sample->time = gclk();
  fp = fopen("/proc/net/dev", "r");
  if (!fp)
    return -1;
  while (fgets(line, sizeof(line), fp)) {
    if (sscanf(line, " %63[^:]: %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64" %"SCNu64,
               iface, &rx, &rx_packets, &rx_errs, &rx_drop, &rx_fifo, &rx_frame, &rx_compressed, &rx_multicast,
               &tx, &tx_packets, &tx_errs, &tx_drop, &tx_fifo, &tx_colls, &tx_carrier, &tx_compressed) != 17)
      continue;
    if (!strcmp(iface, "lo"))
      continue;
    sample->rx_bytes += rx;
    sample->tx_bytes += tx;
  }
  fclose(fp);
  return 0;
}

static void
api_serverinfo_add_network(htsmsg_t *resp)
{
  htsmsg_t *network;
  tvh_sys_net_sample_t sample;
  int64_t elapsed;

  if (api_read_net_sample(&sample))
    return;
  network = htsmsg_create_map();
  htsmsg_add_s64(network, "rx_bytes", sample.rx_bytes);
  htsmsg_add_s64(network, "tx_bytes", sample.tx_bytes);
  if (api_serverinfo_prev_net.time > 0 && sample.time > api_serverinfo_prev_net.time) {
    elapsed = sample.time - api_serverinfo_prev_net.time;
    if (sample.rx_bytes >= api_serverinfo_prev_net.rx_bytes)
      htsmsg_add_s64(network, "rx_bps", (sample.rx_bytes - api_serverinfo_prev_net.rx_bytes) / elapsed);
    if (sample.tx_bytes >= api_serverinfo_prev_net.tx_bytes)
      htsmsg_add_s64(network, "tx_bps", (sample.tx_bytes - api_serverinfo_prev_net.tx_bytes) / elapsed);
  }
  api_serverinfo_prev_net = sample;
  htsmsg_add_msg(resp, "network", network);
}

static void
api_serverinfo_add_recording_storage(htsmsg_t *resp)
{
  static const char *paths[] = { "/recordings", "/var/lib/tvheadend/recordings", NULL };
  struct statvfs st;
  htsmsg_t *storage;
  uint64_t total, free_bytes, available, used;
  int i;

  for (i = 0; paths[i]; i++) {
    if (statvfs(paths[i], &st) == 0)
      break;
  }
  if (!paths[i])
    return;

  total = (uint64_t)st.f_blocks * (uint64_t)st.f_frsize;
  free_bytes = (uint64_t)st.f_bfree * (uint64_t)st.f_frsize;
  available = (uint64_t)st.f_bavail * (uint64_t)st.f_frsize;
  used = total > free_bytes ? total - free_bytes : 0;

  storage = htsmsg_create_map();
  htsmsg_add_str(storage, "path", paths[i]);
  htsmsg_add_s64(storage, "total", total);
  htsmsg_add_s64(storage, "free", free_bytes);
  htsmsg_add_s64(storage, "available", available);
  htsmsg_add_s64(storage, "used", used);
  htsmsg_add_dbl(storage, "used_percent", total ? ((double)used * 100.0) / (double)total : 0.0);
  htsmsg_add_msg(resp, "recording_storage", storage);
}

static void
api_serverinfo_add_system(htsmsg_t *resp)
{
  htsmsg_t *system = htsmsg_create_map();

  api_serverinfo_add_cpu(system);
  api_serverinfo_add_memory(system);
  htsmsg_add_msg(resp, "system", system);
}

static int ah_cmp
  ( api_link_t *a, api_link_t *b )
{
  return strcmp(a->hook->ah_subsystem, b->hook->ah_subsystem);
}

void
api_register ( const api_hook_t *hook )
{
  api_link_t *t;
  SKEL_ALLOC(api_skel);
  api_skel->hook = hook;
  t = RB_INSERT_SORTED(&api_hook_tree, api_skel, link, ah_cmp);
  if (t) {
    tvherror(LS_API, "trying to re-register subsystem");
    free(api_skel);
  } else {
    SKEL_USED(api_skel);
  }
}

void
api_register_all ( const api_hook_t *hooks )
{
  while (hooks->ah_subsystem) {
    api_register(hooks);
    hooks++;
  }
}

int
api_exec ( access_t *perm, const char *subsystem,
           htsmsg_t *args, htsmsg_t **resp )
{
  api_hook_t h;
  api_link_t *ah, skel;
  const char *op;
  uint32_t access;

  /* Args and response must be set */
  if (!args || !resp || !subsystem)
    return EINVAL;

  // Note: there is no locking while checking the hook tree, its assumed
  //       this is all setup during init (if this changes the code will
  //       need updating)
  h.ah_subsystem = subsystem;
  skel.hook      = &h;
  ah = RB_FIND(&api_hook_tree, &skel, link, ah_cmp);

  if (!ah) {
    tvhwarn(LS_API, "failed to find subsystem [%s]", subsystem);
    return ENOSYS; // TODO: is this really the right error code?
  }

  access = ah->hook->ah_access;
  if ((access & ACCESS_NO_EMPTY_ARGS) != 0 && htsmsg_is_empty(args))
    return EPERM;

  if (access_verify2(perm, access & ~ACCESS_INTERNAL))
    return EPERM;

  /* Extract method */
  op = htsmsg_get_str(args, "method");
  if (!op)
    op = htsmsg_get_str(args, "op");
  // Note: this is not required (so no final validation)

  /* Execute */
  return ah->hook->ah_callback(perm, ah->hook->ah_opaque, op, args, resp);
}

static int
api_serverinfo
  ( access_t *perm, void *opaque, const char *op, htsmsg_t *args, htsmsg_t **resp )
{
  *resp = htsmsg_create_map();
  htsmsg_add_str(*resp, "sw_version",   tvheadend_version);
  htsmsg_add_u32(*resp, "api_version",  TVH_API_VERSION);
  htsmsg_add_str(*resp, "name",         "Tvheadend");
  htsmsg_add_s64(*resp, "start_time",   tvheadend_start_time);
  htsmsg_add_s64(*resp, "current_time", gclk());
  htsmsg_add_s64(*resp, "uptime",       MAX(0, gclk() - tvheadend_start_time));
  if (tvheadend_webroot)
    htsmsg_add_str(*resp, "webroot",      tvheadend_webroot);
  htsmsg_add_msg(*resp, "capabilities", tvheadend_capabilities_list(1));
  api_serverinfo_add_system(*resp);
  api_serverinfo_add_network(*resp);
  api_serverinfo_add_recording_storage(*resp);
  return 0;
}

static int
api_server_restart
  ( access_t *perm, void *opaque, const char *op, htsmsg_t *args, htsmsg_t **resp )
{
  *resp = htsmsg_create_map();
  htsmsg_add_str(*resp, "restart", "requested");
  doexit(SIGTERM);
  return 0;
}

static int
api_pathlist
  ( access_t *perm, void *opaque, const char *op, htsmsg_t *args, htsmsg_t **resp )
{
  api_link_t *t;
  *resp = htsmsg_create_list();
  RB_FOREACH(t, &api_hook_tree, link) {
    htsmsg_add_str(*resp, NULL, t->hook->ah_subsystem);
  }
  return 0;
}

void api_init ( void )
{
  static api_hook_t h[] = {
    { "serverinfo", ACCESS_ANONYMOUS, api_serverinfo, NULL },
    { "server/restart", ACCESS_ADMIN, api_server_restart, NULL },
    { "pathlist", ACCESS_ANONYMOUS, api_pathlist, NULL },
    { NULL, 0, NULL, NULL }
  };
  api_register_all(h);

  /* Subsystems */
  api_idnode_init();
  api_idnode_raw_init();
  api_config_init();
  api_input_init();
  api_mpegts_init();
  api_service_init();
  api_channel_init();
  api_bouquet_init();
  api_ratinglabel_init();
  api_epg_init();
  api_epggrab_init();
  api_status_init();
  api_imagecache_init();
  api_esfilter_init();
  api_intlconv_init();
  api_access_init();
  api_dvr_init();
  api_caclient_init();
  api_codec_init();
  api_profile_init();
  api_language_init();
  api_satip_server_init();
  api_timeshift_init();
  api_wizard_init();
}

void api_done ( void )
{
  api_link_t *t;

  while ((t = RB_FIRST(&api_hook_tree)) != NULL) {
    RB_REMOVE(&api_hook_tree, t, link);
    free(t);
  }
  SKEL_FREE(api_skel);
}

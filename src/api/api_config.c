/*
 *  API - General configuration related calls
 *
 *  Copyright (C) 2015 Jaroslav Kysela
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; withm even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "tvheadend.h"
#include "channels.h"
#include "access.h"
#include "memoryinfo.h"
#include "api.h"
#include "config.h"
#include "webhook.h"
#include "htsmsg_json.h"

//Needed to get the current trace/debug states for each subsystem.
//Look in tvhlog.c for more details.
#include "bitops.h"
extern bitops_ulong_t           tvhlog_debug[TVHLOG_BITARRAY];  //TVHLOG_BITARRAY is defined in bitops.h
extern bitops_ulong_t           tvhlog_trace[TVHLOG_BITARRAY];

static int
api_config_capabilities(access_t *perm, void *opaque, const char *op,
                        htsmsg_t *args, htsmsg_t **resp)
{
    *resp = tvheadend_capabilities_list(0);
    return 0;
}

static int
api_webhook_test(access_t *perm, void *opaque, const char *op,
                 htsmsg_t *args, htsmsg_t **resp)
{
  int r = tvh_webhook_test();

  *resp = htsmsg_create_map();
  htsmsg_add_bool(*resp, "success", r == 0);
  if (r == 0)
    htsmsg_add_str(*resp, "message", "Webhook test queued");
  else
    htsmsg_add_str(*resp, "message", strerror(r));
  return 0;
}

static char *
api_webhook_events_to_csv(htsmsg_t *events)
{
  htsmsg_field_t *f;
  htsbuf_queue_t q;
  const char *s;
  int first = 1;

  if (!events)
    return strdup("");
  htsbuf_queue_init(&q, 0);
  HTSMSG_FOREACH(f, events) {
    s = htsmsg_field_get_str(f);
    if (!s)
      continue;
    if (!first)
      htsbuf_append_str(&q, ",");
    htsbuf_append_str(&q, s);
    first = 0;
  }
  return htsbuf_to_string(&q);
}

static void
api_webhook_normalize_events(htsmsg_t *target)
{
  htsmsg_t *events;
  char *copy, *saveptr = NULL, *item;
  const char *csv;

  if (!target || htsmsg_get_list(target, "events"))
    return;
  csv = htsmsg_get_str(target, "events");
  if (!tvh_str_default(csv, NULL))
    return;

  events = htsmsg_create_list();
  copy = strdup(csv);
  for (item = strtok_r(copy, ",", &saveptr); item; item = strtok_r(NULL, ",", &saveptr)) {
    while (*item == ' ' || *item == '\t')
      item++;
    if (*item)
      htsmsg_add_str(events, NULL, item);
  }
  free(copy);
  htsmsg_delete_field(target, "events");
  htsmsg_add_msg(target, "events", events);
}

static int
api_webhook_targets_grid(access_t *perm, void *opaque, const char *op,
                         htsmsg_t *args, htsmsg_t **resp)
{
  htsmsg_t *targets = NULL, *entries, *target, *row, *headers, *events;
  htsmsg_field_t *f;
  char *events_csv, *headers_json;
  const char *s;
  int count = 0;

  entries = htsmsg_create_list();
  if (tvh_str_default(config.webhook_targets, NULL))
    targets = htsmsg_json_deserialize(config.webhook_targets);
  if (targets && targets->hm_islist) {
    HTSMSG_FOREACH(f, targets) {
      target = htsmsg_field_get_map(f);
      if (!target)
        continue;
      row = htsmsg_create_map();
      htsmsg_add_u32(row, "id", count + 1);
      htsmsg_add_bool(row, "enabled", htsmsg_get_bool_or_default(target, "enabled", 1));
      htsmsg_add_str(row, "name", htsmsg_get_str(target, "name") ?: "");
      htsmsg_add_str(row, "url", htsmsg_get_str(target, "url") ?: "");
      htsmsg_add_str(row, "token", htsmsg_get_str(target, "token") ?: "");
      htsmsg_add_str(row, "hmac_secret", htsmsg_get_str(target, "hmac_secret") ?: "");
      htsmsg_add_s32(row, "timeout", htsmsg_get_s32_or_default(target, "timeout", config.webhook_timeout));
      htsmsg_add_s32(row, "retry_count", htsmsg_get_s32_or_default(target, "retry_count", 0));
      htsmsg_add_s32(row, "retry_interval", htsmsg_get_s32_or_default(target, "retry_interval", 1));
      htsmsg_add_bool(row, "ssl_verify", htsmsg_get_bool_or_default(target, "ssl_verify", config.webhook_ssl_verify));
      htsmsg_add_str(row, "template", htsmsg_get_str(target, "template") ?: "");
      events = htsmsg_get_list(target, "events");
      events_csv = events ? api_webhook_events_to_csv(events) : strdup(htsmsg_get_str(target, "events") ?: "");
      htsmsg_add_str(row, "events", events_csv ?: "");
      free(events_csv);
      headers = htsmsg_get_map(target, "headers");
      headers_json = headers ? htsmsg_json_serialize_to_str(headers, 0) : NULL;
      htsmsg_add_str(row, "headers", headers_json ?: "");
      free(headers_json);
      if ((s = htsmsg_get_str(target, "description")) != NULL)
        htsmsg_add_str(row, "description", s);
      htsmsg_add_msg(entries, NULL, row);
      count++;
    }
  }
  if (targets)
    htsmsg_destroy(targets);

  *resp = htsmsg_create_map();
  htsmsg_add_msg(*resp, "entries", entries);
  htsmsg_add_u32(*resp, "totalCount", count);
  return 0;
}

static int
api_webhook_targets_save(access_t *perm, void *opaque, const char *op,
                         htsmsg_t *args, htsmsg_t **resp)
{
  const char *json;
  htsmsg_t *targets, *target;
  htsmsg_field_t *f;
  char *normalized;

  *resp = htsmsg_create_map();
  json = htsmsg_get_str(args, "targets");
  if (!tvh_str_default(json, NULL)) {
    htsmsg_add_bool(*resp, "success", 0);
    htsmsg_add_str(*resp, "message", "Missing targets JSON");
    return 0;
  }
  targets = htsmsg_json_deserialize(json);
  if (!targets || !targets->hm_islist) {
    if (targets)
      htsmsg_destroy(targets);
    htsmsg_add_bool(*resp, "success", 0);
    htsmsg_add_str(*resp, "message", "Targets must be a JSON array");
    return 0;
  }
  HTSMSG_FOREACH(f, targets) {
    target = htsmsg_field_get_map(f);
    if (target)
      api_webhook_normalize_events(target);
  }
  normalized = htsmsg_json_serialize_to_str(targets, 0);
  htsmsg_destroy(targets);
  if (!normalized) {
    htsmsg_add_bool(*resp, "success", 0);
    htsmsg_add_str(*resp, "message", "Unable to serialize targets");
    return 0;
  }
  tvh_str_set(&config.webhook_targets, normalized);
  free(normalized);
  idnode_changed(&config.idnode);
  htsmsg_add_bool(*resp, "success", 1);
  htsmsg_add_str(*resp, "message", "Webhook targets saved");
  return 0;
}

static void
api_memoryinfo_grid
  ( access_t *perm, idnode_set_t *ins, api_idnode_grid_conf_t *conf, htsmsg_t *args )
{
  memoryinfo_t *my;

  LIST_FOREACH(my, &memoryinfo_entries, my_link) {
    if (my->my_update)
      my->my_update(my);
    idnode_set_add(ins, (idnode_t*)my, &conf->filter, perm->aa_lang_ui);
  }
}

//Return a list of trace/debug subsystems and their description.
static int
api_subsystems_grid
 (access_t *perm, void *opaque, const char *op, htsmsg_t *args, htsmsg_t **resp)
{

  int     i;
  int     counter = 0;
  int     trace_count = 0;
  int     debug_count = 0;

  htsmsg_t *api_response;
  htsmsg_t *api_item;

  api_response = htsmsg_create_list();

  tvhlog_subsys_t *ts = tvhlog_subsystems;

  for (i = 1, ts++; i < LS_LAST; i++, ts++)
  {
    api_item = htsmsg_create_map();
    htsmsg_add_u32(api_item, "id", i);
    htsmsg_add_str(api_item, "subsystem", ts->name);
    htsmsg_add_str(api_item, "description", _(ts->desc));

    if(test_bit(i, tvhlog_trace))
    {
      trace_count++;
      htsmsg_add_bool(api_item, "trace", 1);
    }
    else
    {
      htsmsg_add_bool(api_item, "trace", 0);
    }

    if(test_bit(i, tvhlog_debug))
    {
      debug_count++;
      htsmsg_add_bool(api_item, "debug", 1);
    }
    else
    {
      htsmsg_add_bool(api_item, "debug", 0);
    }

    htsmsg_add_msg(api_response, NULL, api_item);

    counter++;
  }

  *resp = htsmsg_create_map();

  htsmsg_add_msg(*resp, "entries", api_response);
  htsmsg_add_u32(*resp, "traceCount", trace_count);
  htsmsg_add_u32(*resp, "debugCount", debug_count);
  htsmsg_add_u32(*resp, "totalCount", counter);

  return 0;
}

void
api_config_init ( void )
{
  static api_hook_t ah[] = {
    { "config/capabilities",    ACCESS_OR|ACCESS_WEB_INTERFACE|ACCESS_HTSP_INTERFACE, api_config_capabilities, NULL },
    { "config/load",            ACCESS_ADMIN, api_idnode_load_simple, &config },
    { "config/save",            ACCESS_ADMIN, api_idnode_save_simple, &config },
    { "webhook/test",           ACCESS_ADMIN, api_webhook_test, NULL },
    { "webhook/targets/grid",   ACCESS_ADMIN, api_webhook_targets_grid, NULL },
    { "webhook/targets/save",   ACCESS_ADMIN, api_webhook_targets_save, NULL },
    { "tvhlog/config/load",     ACCESS_ADMIN, api_idnode_load_simple, &tvhlog_conf },
    { "tvhlog/config/save",     ACCESS_ADMIN, api_idnode_save_simple, &tvhlog_conf },
    { "tvhlog/subsystem/grid",  ACCESS_ADMIN, api_subsystems_grid, NULL },
    { "memoryinfo/class",       ACCESS_ADMIN, api_idnode_class, (void *)&memoryinfo_class },
    { "memoryinfo/grid",        ACCESS_ADMIN, api_idnode_grid, api_memoryinfo_grid },
    { NULL },
  };

  api_register_all(ah);
}

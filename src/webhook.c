/*
 *  tvheadend, Webhook notifications
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/hmac.h>

#include "tvheadend.h"
#include "webhook.h"
#include "config.h"
#include "http.h"
#include "htsmsg_json.h"
#include "url.h"
#include "channels.h"
#include "epg.h"
#include "imagecache.h"
#include "service.h"
#include "streaming.h"
#include "input.h"
#include "lang_str.h"
#include "dvr/dvr.h"
#include "subscriptions.h"

#define TVH_WEBHOOK_MAX_QUEUE 100

typedef struct tvh_webhook_item {
  TAILQ_ENTRY(tvh_webhook_item) link;
  char *target_name;
  char *url;
  char *token;
  char *hmac_secret;
  char *signature_input;
  char *event;
  char *template;
  char *body;
  htsmsg_t *headers;
  int ssl_verify;
  int timeout;
  int retry_count;
  int retry_interval;
} tvh_webhook_item_t;

static TAILQ_HEAD(, tvh_webhook_item) tvh_webhook_queue;
static tvh_mutex_t tvh_webhook_mutex;
static tvh_cond_t tvh_webhook_cond;
static pthread_t tvh_webhook_tid;
static int tvh_webhook_running;
static int tvh_webhook_queue_size;

static void *tvh_webhook_thread(void *aux);

static int
tvh_webhook_enabled(void)
{
  return config.webhook_enabled &&
         (tvh_str_default(config.webhook_url, NULL) != NULL ||
          tvh_str_default(config.webhook_targets, NULL) != NULL);
}

static int
tvh_webhook_is_user_playback_subscription(th_subscription_t *s)
{
  if (!s)
    return 0;
  if (s->ths_title && !strncmp(s->ths_title, "DVR:", 4))
    return 0;
  return s->ths_username || s->ths_hostname || s->ths_client;
}

static char *
tvh_webhook_signature_input(htsmsg_t *m, const char *event)
{
  int64_t timestamp = 0;
  const char *event_id;
  char buf[512];

  event_id = htsmsg_get_str(m, "event_id") ?: "";
  htsmsg_get_s64(m, "timestamp", &timestamp);
  snprintf(buf, sizeof(buf), "%s.%s.%lld",
           event ?: "", event_id, (long long)timestamp);
  return strdup(buf);
}

static int
tvh_webhook_hmac_sha256_hex(const char *secret, const char *input,
                            char *dst, size_t dstlen)
{
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digest_len = 0;

  if (!tvh_str_default(secret, NULL) || !tvh_str_default(input, NULL) ||
      dstlen < 65)
    return EINVAL;
  if (!HMAC(EVP_sha256(), secret, strlen(secret),
            (const unsigned char *)input, strlen(input),
            digest, &digest_len) || digest_len != 32)
    return EIO;
  bin2hex(dst, dstlen, digest, digest_len);
  return 0;
}

static int
tvh_webhook_event_match_one(const char *pattern, const char *event)
{
  size_t n;

  if (!pattern || !event)
    return 0;
  if (!strcmp(pattern, "*") || !strcmp(pattern, event))
    return 1;
  n = strlen(pattern);
  return n > 1 && pattern[n - 1] == '*' &&
         !strncmp(pattern, event, n - 1);
}

static int
tvh_webhook_target_accepts_event(htsmsg_t *target, const char *event)
{
  htsmsg_t *events;
  htsmsg_field_t *f;
  const char *pattern;

  events = htsmsg_get_list(target, "events");
  if (!events)
    return 1;
  HTSMSG_FOREACH(f, events) {
    pattern = htsmsg_field_get_str(f);
    if (tvh_webhook_event_match_one(pattern, event))
      return 1;
  }
  return 0;
}

static char *
tvh_webhook_replace_token(char *src, const char *token, const char *value)
{
  const char *p;
  char *dst, *w;
  size_t src_len, token_len, value_len, count = 0;

  if (!src || !token || !value)
    return src;
  token_len = strlen(token);
  value_len = strlen(value);
  if (token_len == 0)
    return src;
  for (p = src; (p = strstr(p, token)) != NULL; p += token_len)
    count++;
  if (!count)
    return src;
  src_len = strlen(src);
  if (value_len >= token_len)
    dst = malloc(src_len + count * (value_len - token_len) + 1);
  else
    dst = malloc(src_len - count * (token_len - value_len) + 1);
  if (!dst)
    return src;
  w = dst;
  p = src;
  while (1) {
    const char *q = strstr(p, token);
    if (!q)
      break;
    memcpy(w, p, q - p);
    w += q - p;
    memcpy(w, value, value_len);
    w += value_len;
    p = q + token_len;
  }
  strcpy(w, p);
  free(src);
  return dst;
}

static char *
tvh_webhook_apply_template(const char *template, const char *body,
                           const char *event, const char *target_name)
{
  char *out;

  if (!tvh_str_default(template, NULL))
    return strdup(body);
  out = strdup(template);
  if (!out)
    return NULL;
  out = tvh_webhook_replace_token(out, "{{body}}", body ?: "");
  out = tvh_webhook_replace_token(out, "{{event}}", event ?: "");
  out = tvh_webhook_replace_token(out, "{{target}}", target_name ?: "");
  return out;
}

static void
tvh_webhook_item_destroy(tvh_webhook_item_t *item)
{
  if (!item)
    return;
  free(item->target_name);
  free(item->url);
  free(item->token);
  free(item->hmac_secret);
  free(item->signature_input);
  free(item->event);
  free(item->template);
  free(item->body);
  if (item->headers)
    htsmsg_destroy(item->headers);
  free(item);
}

static int
tvh_webhook_queue_item(tvh_webhook_item_t *item)
{
  tvh_mutex_lock(&tvh_webhook_mutex);
  if (!tvh_webhook_running ||
      tvh_webhook_queue_size >= TVH_WEBHOOK_MAX_QUEUE) {
    tvh_mutex_unlock(&tvh_webhook_mutex);
    tvh_webhook_item_destroy(item);
    return ENOBUFS;
  }
  TAILQ_INSERT_TAIL(&tvh_webhook_queue, item, link);
  tvh_webhook_queue_size++;
  tvh_cond_signal(&tvh_webhook_cond, 0);
  tvh_mutex_unlock(&tvh_webhook_mutex);

  return 0;
}

static int
tvh_webhook_enqueue_target(const char *event, const char *body,
                           const char *name, const char *url,
                           const char *token, const char *hmac_secret,
                           const char *signature_input, htsmsg_t *headers,
                           const char *template, int ssl_verify,
                           int timeout, int retry_count,
                           int retry_interval)
{
  tvh_webhook_item_t *item;

  if (!tvh_str_default(url, NULL))
    return EINVAL;
  item = calloc(1, sizeof(*item));
  if (!item)
    return ENOMEM;
  item->target_name = strdup(name ?: "default");
  item->url = strdup(url);
  item->token = tvh_str_default(token, NULL) ? strdup(token) : NULL;
  item->hmac_secret = tvh_str_default(hmac_secret, NULL) ? strdup(hmac_secret) : NULL;
  item->signature_input = tvh_str_default(signature_input, NULL) ? strdup(signature_input) : NULL;
  item->event = strdup(event ?: "");
  item->template = tvh_str_default(template, NULL) ? strdup(template) : NULL;
  item->body = tvh_webhook_apply_template(template, body, event, name);
  item->headers = headers ? htsmsg_copy(headers) : NULL;
  item->ssl_verify = ssl_verify;
  item->timeout = MAX(timeout, 1);
  item->retry_count = MAX(retry_count, 0);
  item->retry_interval = MAX(retry_interval, 1);
  if (!item->target_name || !item->url || !item->event || !item->body ||
      (tvh_str_default(hmac_secret, NULL) && !item->hmac_secret) ||
      (tvh_str_default(signature_input, NULL) && !item->signature_input)) {
    tvh_webhook_item_destroy(item);
    return ENOMEM;
  }
  return tvh_webhook_queue_item(item);
}

static void
tvh_webhook_add_common(htsmsg_t *m, const char *event, const char *event_id)
{
  htsmsg_t *server;

  htsmsg_add_str(m, "source", "tvheadend");
  htsmsg_add_str(m, "event", event);
  htsmsg_add_str(m, "event_id", event_id ?: "");
  htsmsg_add_s64(m, "timestamp", time(NULL));

  server = htsmsg_create_map();
  htsmsg_add_str(server, "name", config_get_server_name());
  htsmsg_add_str(server, "version", tvheadend_version);
  htsmsg_add_msg(m, "server", server);
}

static void
tvh_webhook_add_image(htsmsg_t *m, const char *key, const char *image)
{
  char buf[128];
  const char *s;

  if (strempty(image))
    return;
  s = imagecache_get_propstr(image, buf, sizeof(buf));
  if (s)
    htsmsg_add_str(m, key, s);
}

static void
tvh_webhook_add_program_fields(htsmsg_t *m, channel_t *ch)
{
  epg_broadcast_t *eb;
  const char *s;

  if (!ch)
    return;

  htsmsg_add_uuid(m, "channel_uuid", &ch->ch_id.in_uuid);
  tvh_webhook_add_image(m, "channel_icon", channel_get_icon(ch));

  eb = ch->ch_epg_now;
  if (!eb)
    return;

  htsmsg_add_u32(m, "program_event_id", eb->id);
  htsmsg_add_s64(m, "program_start", eb->start);
  htsmsg_add_s64(m, "program_stop", eb->stop);
  if ((s = epg_broadcast_get_title(eb, NULL)))
    htsmsg_add_str(m, "program_title", s);
  if ((s = epg_broadcast_get_subtitle(eb, NULL)))
    htsmsg_add_str(m, "program_subtitle", s);
  if ((s = epg_broadcast_get_summary(eb, NULL)))
    htsmsg_add_str(m, "program_summary", s);
  if ((s = epg_broadcast_get_description(eb, NULL)))
    htsmsg_add_str(m, "program_description", s);
  tvh_webhook_add_image(m, "program_image", eb->image);
}

static void
tvh_webhook_add_subscription_fields(htsmsg_t *m, th_subscription_t *s)
{
  char buf[284];

  htsmsg_add_u32(m, "subscription_id", s->ths_id);
  htsmsg_add_s64(m, "started", s->ths_start);
  htsmsg_add_u32(m, "errors", atomic_get(&s->ths_total_err));
  htsmsg_add_u32(m, "input_kbps", atomic_get(&s->ths_bytes_in_avg));
  htsmsg_add_u32(m, "output_kbps", atomic_get(&s->ths_bytes_out_avg));

  if (s->ths_username)
    htsmsg_add_str(m, "user", s->ths_username);
  if (s->ths_hostname)
    htsmsg_add_str(m, "ip", s->ths_hostname);
  if (s->ths_client)
    htsmsg_add_str(m, "client", s->ths_client);
  if (s->ths_title)
    htsmsg_add_str(m, "title", s->ths_title);
  if (s->ths_channel) {
    htsmsg_add_str(m, "channel",
                   channel_get_name(s->ths_channel, channel_blank_name));
    tvh_webhook_add_program_fields(m, s->ths_channel);
  }
  if (s->ths_service)
    htsmsg_add_str(m, "service",
                   service_adapter_nicename(s->ths_service, buf, sizeof(buf)));
}

static htsmsg_t *
tvh_webhook_subscription_msg(const char *event, th_subscription_t *s)
{
  htsmsg_t *m = htsmsg_create_map();
  char event_id[64];

  snprintf(event_id, sizeof(event_id), "subscription-%d-%ld",
           s->ths_id, (long)time(NULL));
  tvh_webhook_add_common(m, event, event_id);
  tvh_webhook_add_subscription_fields(m, s);
  return m;
}

static htsmsg_t *
tvh_webhook_dvr_msg(const char *event, dvr_entry_t *de)
{
  htsmsg_t *m = htsmsg_create_map();
  char ubuf[UUID_HEX_SIZE];
  char event_id[UUID_HEX_SIZE + 32];
  const char *title;

  idnode_uuid_as_str(&de->de_id, ubuf);
  snprintf(event_id, sizeof(event_id), "dvr-%s-%ld", ubuf, (long)time(NULL));
  tvh_webhook_add_common(m, event, event_id);

  htsmsg_add_str(m, "dvr_uuid", ubuf);
  htsmsg_add_str(m, "sched_state", dvr_entry_sched_state2str(de->de_sched_state));
  htsmsg_add_str(m, "recording_state", dvr_entry_rs_state2str(de->de_rec_state));
  htsmsg_add_u32(m, "errors", de->de_errors);
  htsmsg_add_u32(m, "data_errors", de->de_data_errors);
  htsmsg_add_u32(m, "last_error", de->de_last_error);
  htsmsg_add_str(m, "last_error_text", streaming_code2txt(de->de_last_error));
  htsmsg_add_s64(m, "start", de->de_start);
  htsmsg_add_s64(m, "stop", de->de_stop);

  title = lang_str_get(de->de_title, NULL);
  if (title)
    htsmsg_add_str(m, "title", title);
  if (de->de_subtitle && lang_str_get(de->de_subtitle, NULL))
    htsmsg_add_str(m, "subtitle", lang_str_get(de->de_subtitle, NULL));
  if (DVR_CH_NAME(de))
    htsmsg_add_str(m, "channel", DVR_CH_NAME(de));
  if (dvr_get_filename(de))
    htsmsg_add_str(m, "filename", dvr_get_filename(de));
  if (de->de_owner)
    htsmsg_add_str(m, "user", de->de_owner);

  return m;
}

static htsmsg_t *
tvh_webhook_service_error_msg(service_t *s, int flags)
{
  htsmsg_t *m = htsmsg_create_map();
  char ubuf[UUID_HEX_SIZE];
  char event_id[UUID_HEX_SIZE + 32];
  char adapter[284];

  idnode_uuid_as_str(&s->s_id, ubuf);
  snprintf(event_id, sizeof(event_id), "service-%s-%x", ubuf, flags);
  tvh_webhook_add_common(m, "service.error", event_id);

  htsmsg_add_str(m, "service_uuid", ubuf);
  htsmsg_add_s32(m, "status_flags", flags);
  htsmsg_add_str(m, "status_text", service_tss2text(flags));
  htsmsg_add_str(m, "service", service_nicename(s));
  htsmsg_add_str(m, "adapter", service_adapter_nicename(s, adapter, sizeof(adapter)));

  return m;
}

static htsmsg_t *
tvh_webhook_dvb_error_msg(mpegts_input_t *mi, mpegts_mux_t *mm, int flags)
{
  htsmsg_t *m = htsmsg_create_map();
  char input_uuid[UUID_HEX_SIZE], mux_uuid[UUID_HEX_SIZE];
  char event_id[UUID_HEX_SIZE * 2 + 40];
  char input_name[256], mux_name[256];

  idnode_uuid_as_str(&mi->ti_id, input_uuid);
  idnode_uuid_as_str(&mm->mm_id, mux_uuid);
  snprintf(event_id, sizeof(event_id), "dvb-%s-%s-%x",
           input_uuid, mux_uuid, flags);
  tvh_webhook_add_common(m, "dvb.error", event_id);

  input_name[0] = '\0';
  mux_name[0] = '\0';
  if (mi->mi_display_name)
    mi->mi_display_name(mi, input_name, sizeof(input_name));
  if (mm->mm_display_name)
    mm->mm_display_name(mm, mux_name, sizeof(mux_name));

  htsmsg_add_str(m, "input_uuid", input_uuid);
  htsmsg_add_str(m, "mux_uuid", mux_uuid);
  htsmsg_add_str(m, "input", input_name);
  htsmsg_add_str(m, "mux", mux_name);
  htsmsg_add_s32(m, "status_flags", flags);
  htsmsg_add_str(m, "status_text", service_tss2text(flags));

  return m;
}

static int
tvh_webhook_enqueue_msg(const char *event, htsmsg_t *m)
{
  htsmsg_t *targets = NULL, *target, *headers;
  htsmsg_field_t *f;
  char *body, *signature_input;
  const char *name, *url, *token, *hmac_secret, *template;
  int queued = 0, failed = 0, have_targets_config = 0;
  int enabled, ssl_verify, timeout, retry_count, retry_interval;

  if (!tvh_webhook_enabled()) {
    htsmsg_destroy(m);
    return EINVAL;
  }

  signature_input = tvh_webhook_signature_input(m, event);
  body = htsmsg_json_serialize_to_str(m, 0);
  htsmsg_destroy(m);
  if (!body || !signature_input) {
    free(body);
    free(signature_input);
    return ENOMEM;
  }

  if (tvh_str_default(config.webhook_targets, NULL))
    targets = htsmsg_json_deserialize(config.webhook_targets);

  if (targets && targets->hm_islist) {
    have_targets_config = 1;
    HTSMSG_FOREACH(f, targets) {
      target = htsmsg_field_get_map(f);
      if (!target)
        continue;
      enabled = htsmsg_get_bool_or_default(target, "enabled", 1);
      if (!enabled || !tvh_webhook_target_accepts_event(target, event))
        continue;
      name = htsmsg_get_str(target, "name");
      url = htsmsg_get_str(target, "url");
      token = htsmsg_get_str(target, "token");
      hmac_secret = htsmsg_get_str(target, "hmac_secret");
      template = htsmsg_get_str(target, "template");
      headers = htsmsg_get_map(target, "headers");
      ssl_verify = htsmsg_get_bool_or_default(target, "ssl_verify", config.webhook_ssl_verify);
      timeout = htsmsg_get_s32_or_default(target, "timeout", config.webhook_timeout);
      retry_count = htsmsg_get_s32_or_default(target, "retry_count", 0);
      retry_interval = htsmsg_get_s32_or_default(target, "retry_interval", 1);
      if (tvh_webhook_enqueue_target(event, body, name, url, token,
                                     hmac_secret, signature_input, headers,
                                     template, ssl_verify, timeout,
                                     retry_count, retry_interval) == 0)
        queued++;
      else
        failed++;
    }
  }

  if (targets)
    htsmsg_destroy(targets);

  if (!queued && !have_targets_config && tvh_str_default(config.webhook_url, NULL)) {
    if (tvh_webhook_enqueue_target(event, body, "default",
                                   config.webhook_url, config.webhook_token,
                                   NULL, signature_input, NULL, NULL, config.webhook_ssl_verify,
                                   config.webhook_timeout, 0, 1) == 0)
      queued++;
    else
      failed++;
  }

  free(body);
  free(signature_input);
  if (queued)
    return 0;
  return failed ? ENOBUFS : EINVAL;
}

void
tvh_webhook_subscription_start(th_subscription_t *s)
{
  if (config.webhook_notify_playback &&
      tvh_webhook_enabled() &&
      tvh_webhook_is_user_playback_subscription(s) &&
      tvh_webhook_enqueue_msg("playback.start", tvh_webhook_subscription_msg("playback.start", s)) == 0)
    s->ths_webhook_started = 1;
}

void
tvh_webhook_subscription_stop(th_subscription_t *s)
{
  if (config.webhook_notify_playback &&
      tvh_webhook_enabled() &&
      s->ths_webhook_started &&
      tvh_webhook_is_user_playback_subscription(s))
    tvh_webhook_enqueue_msg("playback.stop", tvh_webhook_subscription_msg("playback.stop", s));
  s->ths_webhook_started = 0;
}

void
tvh_webhook_dvr_event(dvr_entry_t *de, tvh_webhook_dvr_event_t event)
{
  const char *name;

  if (!config.webhook_notify_dvr || !tvh_webhook_enabled())
    return;

  switch (event) {
  case TVH_WEBHOOK_DVR_START:
    name = "dvr.start";
    break;
  case TVH_WEBHOOK_DVR_COMPLETE:
    name = "dvr.complete";
    break;
  case TVH_WEBHOOK_DVR_ERROR:
  default:
    name = "dvr.error";
    break;
  }
  tvh_webhook_enqueue_msg(name, tvh_webhook_dvr_msg(name, de));
}

void
tvh_webhook_service_error(service_t *s, int flags)
{
  if (!config.webhook_notify_errors || !tvh_webhook_enabled() || !s ||
      !service_tss_is_error(flags))
    return;
  tvh_webhook_enqueue_msg("service.error",
                          tvh_webhook_service_error_msg(s, flags));
}

void
tvh_webhook_dvb_error(mpegts_input_t *mi, mpegts_mux_t *mm, int flags)
{
  if (!config.webhook_notify_errors || !tvh_webhook_enabled() || !mi || !mm ||
      !service_tss_is_error(flags))
    return;
  tvh_webhook_enqueue_msg("dvb.error",
                          tvh_webhook_dvb_error_msg(mi, mm, flags));
}

int
tvh_webhook_test(void)
{
  htsmsg_t *m = htsmsg_create_map();

  tvh_webhook_add_common(m, "system.webhooktest", "webhook-test");
  htsmsg_add_str(m, "title", "TVH Webhook test");
  htsmsg_add_str(m, "message", "Webhook test message from Tvheadend");
  return tvh_webhook_enqueue_msg("system.webhooktest", m);
}

static int
tvh_webhook_post(tvh_webhook_item_t *item)
{
  url_t url;
  http_client_t *hc = NULL;
  http_arg_list_t h;
  htsmsg_field_t *f;
  int r, timeout, elapsed;
  char len[32];
  char signature[80];
  const char *key, *val;

  urlinit(&url);
  if (urlparse(item->url, &url)) {
    tvherror(LS_HTTPC, "webhook: invalid url '%s'", item->url);
    return EINVAL;
  }

  hc = http_client_connect(NULL, HTTP_VERSION_1_1, url.scheme,
                           url.host, url.port, NULL);
  if (!hc) {
    urlreset(&url);
    return EIO;
  }
  http_client_ssl_peer_verify(hc, item->ssl_verify);

  http_arg_init(&h);
  http_client_basic_args(hc, &h, &url, 0);
  http_arg_set(&h, "Content-Type", "application/json");
  snprintf(len, sizeof(len), "%zu", strlen(item->body));
  http_arg_set(&h, "Content-Length", len);
  if (tvh_str_default(item->token, NULL))
    http_arg_set(&h, "X-Tvh-Token", item->token);
  if (tvh_str_default(item->hmac_secret, NULL) &&
      tvh_str_default(item->signature_input, NULL) &&
      tvh_webhook_hmac_sha256_hex(item->hmac_secret, item->signature_input,
                                  signature + 7, sizeof(signature) - 7) == 0) {
    const char *timestamp = strrchr(item->signature_input, '.');
    memcpy(signature, "sha256=", 7);
    http_arg_set(&h, "X-Tvh-Signature", signature);
    http_arg_set(&h, "X-Tvh-Signature-Input", item->signature_input);
    if (timestamp && timestamp[1])
      http_arg_set(&h, "X-Tvh-Timestamp", timestamp + 1);
  }
  if (item->headers) {
    HTSMSG_FOREACH(f, item->headers) {
      key = htsmsg_field_name(f);
      val = htsmsg_field_get_str(f);
      if (tvh_str_default(key, NULL) && tvh_str_default(val, NULL))
        http_arg_set(&h, key, val);
    }
  }

  tvh_mutex_lock(&hc->hc_mutex);
  r = http_client_send(hc, HTTP_CMD_POST, url.path, url.query,
                       &h, (void *)item->body, strlen(item->body));
  tvh_mutex_unlock(&hc->hc_mutex);

  timeout = MAX(item->timeout, 1) * 20;
  elapsed = 0;
  while (r == HTTP_CON_RECEIVING || r == HTTP_CON_SENDING ||
         r == HTTP_CON_SENT || r == HTTP_CON_IDLE) {
    if (++elapsed > timeout) {
      r = -ETIMEDOUT;
      break;
    }
    tvh_safe_usleep(50000);
    r = http_client_run(hc);
  }

  if (hc->hc_code >= 200 && hc->hc_code < 300) {
    tvhdebug(LS_HTTPC, "webhook: delivered event '%s' to target '%s'",
             item->event, item->target_name);
    r = HTTP_CON_DONE;
  } else if (r == HTTP_CON_DONE) {
    tvhwarn(LS_HTTPC, "webhook: target '%s' returned HTTP %d",
            item->target_name, hc->hc_code);
  } else if (r < 0) {
    tvhwarn(LS_HTTPC, "webhook: target '%s' delivery failed: %s",
            item->target_name, strerror(-r));
  } else if (r > 0) {
    tvhwarn(LS_HTTPC, "webhook: target '%s' delivery failed with client state %d",
            item->target_name, r);
  }

  http_arg_flush(&h);
  http_client_close(hc);
  urlreset(&url);
  return r == HTTP_CON_DONE ? 0 : EIO;
}

static void *
tvh_webhook_thread(void *aux)
{
  tvh_webhook_item_t *item;

  tvh_mutex_lock(&tvh_webhook_mutex);
  while (tvh_webhook_running) {
    item = TAILQ_FIRST(&tvh_webhook_queue);
    if (!item) {
      tvh_cond_wait(&tvh_webhook_cond, &tvh_webhook_mutex);
      continue;
    }
    TAILQ_REMOVE(&tvh_webhook_queue, item, link);
    tvh_webhook_queue_size--;
    tvh_mutex_unlock(&tvh_webhook_mutex);

    if (tvh_webhook_enabled()) {
      int attempt, result = EIO;
      for (attempt = 0; attempt <= item->retry_count; attempt++) {
        result = tvh_webhook_post(item);
        if (result == 0)
          break;
        if (attempt < item->retry_count)
          tvh_safe_usleep((int64_t)item->retry_interval * 1000000);
      }
      if (result)
        tvhwarn(LS_HTTPC, "webhook: target '%s' dropped event '%s' after %d attempt(s)",
                item->target_name, item->event, item->retry_count + 1);
    }

    tvh_webhook_item_destroy(item);
    tvh_mutex_lock(&tvh_webhook_mutex);
  }
  tvh_mutex_unlock(&tvh_webhook_mutex);

  return NULL;
}

void
tvh_webhook_init(void)
{
  TAILQ_INIT(&tvh_webhook_queue);
  tvh_mutex_init(&tvh_webhook_mutex, NULL);
  tvh_cond_init(&tvh_webhook_cond, 1);
  tvh_webhook_running = 1;
  tvh_thread_create(&tvh_webhook_tid, NULL, tvh_webhook_thread, NULL, "webhook");
}

void
tvh_webhook_done(void)
{
  tvh_webhook_item_t *item;

  tvh_mutex_lock(&tvh_webhook_mutex);
  tvh_webhook_running = 0;
  tvh_cond_signal(&tvh_webhook_cond, 0);
  tvh_mutex_unlock(&tvh_webhook_mutex);
  pthread_join(tvh_webhook_tid, NULL);

  tvh_mutex_lock(&tvh_webhook_mutex);
  while ((item = TAILQ_FIRST(&tvh_webhook_queue)) != NULL) {
    TAILQ_REMOVE(&tvh_webhook_queue, item, link);
    tvh_webhook_item_destroy(item);
  }
  tvh_webhook_queue_size = 0;
  tvh_mutex_unlock(&tvh_webhook_mutex);
}

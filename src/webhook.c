/*
 *  tvheadend, Webhook notifications
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tvheadend.h"
#include "webhook.h"
#include "config.h"
#include "http.h"
#include "htsmsg_json.h"
#include "url.h"
#include "channels.h"
#include "service.h"
#include "streaming.h"
#include "lang_str.h"
#include "dvr/dvr.h"
#include "subscriptions.h"

#define TVH_WEBHOOK_MAX_QUEUE 100

typedef struct tvh_webhook_item {
  TAILQ_ENTRY(tvh_webhook_item) link;
  char *body;
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
         tvh_str_default(config.webhook_url, NULL) != NULL;
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
  if (s->ths_channel)
    htsmsg_add_str(m, "channel",
                   channel_get_name(s->ths_channel, channel_blank_name));
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

static int
tvh_webhook_enqueue_msg(htsmsg_t *m)
{
  tvh_webhook_item_t *item;
  char *body;

  if (!tvh_webhook_enabled()) {
    htsmsg_destroy(m);
    return EINVAL;
  }

  body = htsmsg_json_serialize_to_str(m, 0);
  htsmsg_destroy(m);
  if (!body)
    return ENOMEM;

  item = calloc(1, sizeof(*item));
  if (!item) {
    free(body);
    return ENOMEM;
  }
  item->body = body;

  tvh_mutex_lock(&tvh_webhook_mutex);
  if (!tvh_webhook_running ||
      tvh_webhook_queue_size >= TVH_WEBHOOK_MAX_QUEUE) {
    tvh_mutex_unlock(&tvh_webhook_mutex);
    free(item->body);
    free(item);
    return ENOBUFS;
  }
  TAILQ_INSERT_TAIL(&tvh_webhook_queue, item, link);
  tvh_webhook_queue_size++;
  tvh_cond_signal(&tvh_webhook_cond, 0);
  tvh_mutex_unlock(&tvh_webhook_mutex);

  return 0;
}

void
tvh_webhook_subscription_start(th_subscription_t *s)
{
  if (config.webhook_notify_playback &&
      tvh_webhook_enabled() &&
      (s->ths_username || s->ths_hostname || s->ths_client) &&
      tvh_webhook_enqueue_msg(tvh_webhook_subscription_msg("playback.start", s)) == 0)
    s->ths_webhook_started = 1;
}

void
tvh_webhook_subscription_stop(th_subscription_t *s)
{
  if (config.webhook_notify_playback &&
      tvh_webhook_enabled() &&
      s->ths_webhook_started &&
      (s->ths_username || s->ths_hostname || s->ths_client))
    tvh_webhook_enqueue_msg(tvh_webhook_subscription_msg("playback.stop", s));
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
  tvh_webhook_enqueue_msg(tvh_webhook_dvr_msg(name, de));
}

int
tvh_webhook_test(void)
{
  htsmsg_t *m = htsmsg_create_map();

  tvh_webhook_add_common(m, "system.webhooktest", "webhook-test");
  htsmsg_add_str(m, "title", "TVH Webhook test");
  htsmsg_add_str(m, "message", "Webhook test message from Tvheadend");
  return tvh_webhook_enqueue_msg(m);
}

static int
tvh_webhook_post(const char *body)
{
  url_t url;
  http_client_t *hc = NULL;
  http_arg_list_t h;
  int r, timeout, elapsed;
  char len[32];

  urlinit(&url);
  if (urlparse(config.webhook_url, &url)) {
    tvherror(LS_HTTPC, "webhook: invalid url '%s'", config.webhook_url);
    return EINVAL;
  }

  hc = http_client_connect(NULL, HTTP_VERSION_1_1, url.scheme,
                           url.host, url.port, NULL);
  if (!hc) {
    urlreset(&url);
    return EIO;
  }
  http_client_ssl_peer_verify(hc, config.webhook_ssl_verify);

  http_arg_init(&h);
  http_client_basic_args(hc, &h, &url, 0);
  http_arg_set(&h, "Content-Type", "application/json");
  snprintf(len, sizeof(len), "%zu", strlen(body));
  http_arg_set(&h, "Content-Length", len);
  if (tvh_str_default(config.webhook_token, NULL))
    http_arg_set(&h, "X-Tvh-Token", config.webhook_token);

  tvh_mutex_lock(&hc->hc_mutex);
  r = http_client_send(hc, HTTP_CMD_POST, url.path, url.query,
                       &h, (void *)body, strlen(body));
  tvh_mutex_unlock(&hc->hc_mutex);

  timeout = MAX(config.webhook_timeout, 1) * 20;
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
    tvhdebug(LS_HTTPC, "webhook: delivered event to %s", config.webhook_url);
    r = HTTP_CON_DONE;
  } else if (r == HTTP_CON_DONE) {
    tvhwarn(LS_HTTPC, "webhook: endpoint returned HTTP %d", hc->hc_code);
  } else if (r < 0) {
    tvhwarn(LS_HTTPC, "webhook: delivery failed: %s", strerror(-r));
  } else if (r > 0) {
    tvhwarn(LS_HTTPC, "webhook: delivery failed with client state %d", r);
  }

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

    if (tvh_webhook_enabled())
      tvh_webhook_post(item->body);

    free(item->body);
    free(item);
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
    free(item->body);
    free(item);
  }
  tvh_webhook_queue_size = 0;
  tvh_mutex_unlock(&tvh_webhook_mutex);
}

/*
 *  tvheadend, Webhook notifications
 */

#ifndef WEBHOOK_H_
#define WEBHOOK_H_

struct th_subscription;
struct dvr_entry;
struct service;
struct mpegts_input;
struct mpegts_mux;

typedef enum {
  TVH_WEBHOOK_DVR_START,
  TVH_WEBHOOK_DVR_COMPLETE,
  TVH_WEBHOOK_DVR_ERROR
} tvh_webhook_dvr_event_t;

void tvh_webhook_init(void);
void tvh_webhook_done(void);

void tvh_webhook_subscription_start(struct th_subscription *s);
void tvh_webhook_subscription_stop(struct th_subscription *s);
void tvh_webhook_dvr_event(struct dvr_entry *de, tvh_webhook_dvr_event_t event);
void tvh_webhook_service_error(struct service *s, int flags);
void tvh_webhook_dvb_error(struct mpegts_input *mi, struct mpegts_mux *mm, int flags);

int tvh_webhook_test(void);

#endif /* WEBHOOK_H_ */

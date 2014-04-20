#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/util.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/bufferevent_ssl.h>
#include <event2/bufferevent_struct.h>
#include <event2/keyvalq_struct.h>
#include <signal.h>
#include <getopt.h>
#include <sys/queue.h>
#include "config.h"
#include "city.h"

struct channel;

struct connection {
  const char *callback;
  struct evhttp_request *req;
  struct channel *channel;

  TAILQ_ENTRY(connection) next;
};

struct channel {
  char *name;
  uint64_t hash;
  uint length;
  
  TAILQ_HEAD(,connection) connections;

  TAILQ_ENTRY(channel) next;
};

TAILQ_HEAD(,channel) channels;

struct evhttp_bound_socket *handle;
int verbose = 0;

struct channel* get_channel(uint64_t hash) {
    struct channel *channel; 

    TAILQ_FOREACH(channel, &channels, next) {
      if (channel->hash == hash) {
        return channel;
      }
    }

    return NULL;
}

int get_int(struct evkeyvalq *params, const char *name, int def)
{
  const char *val = evhttp_find_header(params, name);
  return val ? atoi(val) : def;
}

const char* get_str(struct evkeyvalq *params, const char *name, const char *def)
{
  const char *val = evhttp_find_header(params, name);
  return val ? val : def;
}

static void close(struct connection *connection)
{
  if (connection->req->evcon) {
    evhttp_connection_set_closecb(connection->req->evcon, NULL, NULL);
  }

  evhttp_send_reply_end(connection->req);

  TAILQ_REMOVE(&connection->channel->connections, connection, next);

  if (connection->channel->length == 1) {
    TAILQ_REMOVE(&channels, connection->channel, next);
    free(connection->channel->name);
    free(connection->channel);
  } else {
    connection->channel->length--;
  }

  free(connection);
}

static void disconnect_cb(struct evhttp_connection *conn, void *arg)
{
  struct connection *connection = (struct connection *)arg;

  if (verbose)
    fprintf(stderr, "disconnected %p\n", connection);

  close(connection);
}

static void pub_handler(struct evhttp_request *req, void *arg)
{
  const char *ruri = evhttp_request_get_uri(req);

  if (verbose)
    fprintf(stderr, "new pub request URI '%s'\n", ruri);

  if (evhttp_request_get_command(req) != EVHTTP_REQ_GET) {
    evhttp_send_reply(req, HTTP_BADMETHOD, "Invalid Method", NULL);
    return;
  }

  struct evbuffer *buf;

  struct evkeyvalq params;
  evhttp_parse_query(ruri, &params);

  const char *name = get_str(&params, "channel", NULL);
  if (name == NULL || strlen(name) == 0) {
    evhttp_send_reply(req, HTTP_BADREQUEST, "Invalid Request", NULL);

    evhttp_clear_headers(&params);
    return;
  }

  uint64_t hash = CityHash64(name, strlen(name));
  struct channel *channel = get_channel(hash);
  if (channel == NULL) {
    buf = evbuffer_new();
    evbuffer_add_printf(buf, "{sent: 0}\n");
    evhttp_send_reply(req, HTTP_OK, "OK", buf);
    evbuffer_free(buf);

    evhttp_clear_headers(&params);
    return;
  }

  uint length = channel->length;

  struct connection *connection;
  TAILQ_FOREACH(connection, &channel->connections, next) {
    
    if (verbose)
      fprintf(stderr, "notify %p\n", connection->req->evcon);

    buf = evbuffer_new();
    if (connection->callback) {
      evbuffer_add_printf(buf, "%s(", connection->callback);
    } 
    evbuffer_add_printf(buf, "{content: \"%s\"}", "X");
    if (connection->callback) {
      evbuffer_add_printf(buf, ");");
    }
    evbuffer_add_printf(buf, "\n");

    evhttp_send_reply_chunk(connection->req, buf);

    evbuffer_free(buf);

    close(connection);
  }

  evhttp_add_header(req->output_headers, "Content-Type", "text/json; charset=utf-8");
  evhttp_add_header(req->output_headers, "Access-Control-Allow-Origin", "*");
  evhttp_add_header(req->output_headers, "Access-Control-Allow-Methods", "GET,POST");

  buf = evbuffer_new();
  evbuffer_add_printf(buf, "{sent: %d}\n", length);
  evhttp_send_reply(req, HTTP_OK, "OK", buf);
  evbuffer_free(buf);

  evhttp_clear_headers(&params);
}

static void sub_handler(struct evhttp_request *req, void *arg)
{
  const char *ruri = evhttp_request_get_uri(req);

  if (verbose)
    fprintf(stderr, "new sub request URI '%s'\n", ruri);

  if (evhttp_request_get_command(req) != EVHTTP_REQ_GET) {
    evhttp_send_reply(req, HTTP_BADMETHOD, "Invalid Method", NULL);
    return;
  }

  struct evkeyvalq params;
  evhttp_parse_query(ruri, &params);

  const char *name = get_str(&params, "channel", NULL);
  if (name == NULL || strlen(name) == 0) {
    evhttp_send_reply(req, HTTP_BADREQUEST, "Invalid Request", NULL);
    return;
  }

  uint64_t hash = CityHash64(name, strlen(name));
  struct channel *channel = get_channel(hash);
  if (channel == NULL) {
    channel = (struct channel *) calloc(1, sizeof *channel);
    channel->length = 0;
    channel->name = strdup(name);
    channel->hash = hash;
    TAILQ_INIT(&channel->connections);
    TAILQ_INSERT_TAIL(&channels, channel, next);
  }

  struct connection *connection;
  connection = (struct connection *) calloc(1, sizeof *connection);
  connection->req = req;
  connection->callback = get_str(&params, "callback", NULL);
  connection->channel = channel;

  channel->length++;
  TAILQ_INSERT_TAIL(&channel->connections, connection, next);

  struct bufferevent * bufev = evhttp_connection_get_bufferevent(req->evcon);
  bufferevent_enable(bufev, EV_READ);
  evhttp_connection_set_closecb(req->evcon, disconnect_cb, connection);
  evhttp_add_header(req->output_headers, "Connection", "keep-alive");
  evhttp_add_header(req->output_headers, "Content-Type", "text/html; charset=utf-8");
  evhttp_add_header(req->output_headers, "Access-Control-Allow-Origin", "*");
  evhttp_add_header(req->output_headers, "Access-Control-Allow-Methods", "GET,POST");
 
  evhttp_send_reply_start(req, HTTP_OK, "OK");

  evhttp_clear_headers(&params);

  if (verbose)
    fprintf(stderr, "connected %p\n", connection->req->evcon);
}

static void gen_handler(struct evhttp_request *req, void *arg)
{
  const char *ruri = evhttp_request_get_uri(req);

  if (verbose)
    fprintf(stderr, "unrecognized request URI '%s', sending 404\n", ruri);

  evhttp_send_reply(req, HTTP_BADREQUEST, "Bad Request", NULL);
}

static void sigint_cb(evutil_socket_t sig, short events, void *ptr)
{
  struct event_base *base = (event_base *)ptr;
  struct timeval delay = { 0, 0 };
 
  printf("Interrupted exiting...\n");
  event_base_loopexit(base, &delay);
}

static void sighup_cb(evutil_socket_t sig, short events, void *ptr)
{
  printf("HUP\n");
}

int main (int argc, char *argv[])
{
  const char *address = "0.0.0.0";
  int port = 8080;
  struct event_base *base;
  struct evhttp *server;
  struct event *sigint_event, *sighup_event;

  int c;
  while ((c = getopt (argc, argv, "vha:p:")) != -1) {
    switch (c) {
      case 'v':
        verbose = 1;
        break;
      case 'a':
        address = optarg;
        break;
      case 'p':
        port = atoi(optarg);
        break;
      case 'h':
      default:
        fprintf(stderr, "usage: %s [-v] [-a 0.0.0.0] [-p 8080]\n", argv[0]);
        exit(1);
    }
  }

  TAILQ_INIT(&channels);

  base = event_base_new();

  sigint_event = evsignal_new(base, SIGINT, sigint_cb, base);
  if (!sigint_event || event_add(sigint_event, NULL) < 0) {
    fprintf(stderr, "Could not create or add the SIGINT signal event.\n");
    return -1;
  }

  if (verbose)
    fprintf(stderr, "SIGINT bound to exit\n");

  sighup_event = evsignal_new(base, SIGHUP, sighup_cb, NULL);
  if (!sighup_event || event_add(sighup_event, NULL) < 0) {
    fprintf(stderr, "Could not create or add the SIGHUP signal event.\n");
    return -1;
  }

  if (verbose)
    fprintf(stderr, "SIGHUP bound to clear queue\n");

  server = evhttp_new(base);

  evhttp_set_cb(server, "/pub", pub_handler, NULL);
  evhttp_set_cb(server, "/sub", sub_handler, NULL);
  evhttp_set_gencb(server, gen_handler, NULL);

  handle = evhttp_bind_socket_with_handle(server, address, port);

  if (verbose)
    fprintf(stderr, "Server bound to %s:%d\n", address, port);

  if (verbose)
    fprintf(stderr, "Entering dispatching loop, server started\n");

  event_base_dispatch(base);

  event_free(sigint_event);
  event_free(sighup_event);
  evhttp_free(server);
  event_base_free(base);

  return 0;
}

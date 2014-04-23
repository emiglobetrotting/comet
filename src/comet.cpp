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
#include <arpa/inet.h>
#include "comet.h"
#include "config.h"
#include "city.h"

struct stat stats;
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
  stats.active_connection--;
}

static void disconnect_cb(struct evhttp_connection *conn, void *arg)
{
  struct connection *connection = (struct connection *)arg;

  if (verbose)
    fprintf(stderr, "disconnected %p\n", connection);

  close(connection);
}

static void stat_handler(struct evhttp_request *req, void *arg)
{
  const char *ruri = evhttp_request_get_uri(req);

  if (verbose)
    fprintf(stderr, "new status request URI '%s'\n", ruri);

  if (check_ip(req->remote_host) == 0) {
    fprintf(stderr, "not allowed '%s:%d'\n", req->remote_host, req->remote_port);

    evhttp_add_header(req->output_headers, "Connection", "Close");
    evhttp_send_reply(req, HTTP_FORBIDDEN, "Forbidden", NULL);
    stats.forbidden++;
    return;
  }
  
  struct evbuffer *buf;

  evhttp_add_header(req->output_headers, "Content-Type", "text/json; charset=utf-8");

  buf = evbuffer_new();
  evbuffer_add_printf(buf, "{publication: %" PRIu64 ",", stats.publication);
  evbuffer_add_printf(buf, "notification: %" PRIu64 ",", stats.notification);
  evbuffer_add_printf(buf, "connection: %" PRIu64 ",", stats.connection);
  evbuffer_add_printf(buf, "active_connection: %" PRIu64 ",", stats.active_connection);
  evbuffer_add_printf(buf, "invalid_method: %" PRIu64 ",", stats.invalid_method);
  evbuffer_add_printf(buf, "invalid_request: %" PRIu64 ",", stats.invalid_request);
  evbuffer_add_printf(buf, "unknown_request: %" PRIu64 ",", stats.unknown_request);
  evbuffer_add_printf(buf, "forbidden: %" PRIu64 "}", stats.forbidden);
  evhttp_send_reply(req, HTTP_OK, "OK", buf);
  evbuffer_free(buf);
}

static void pub_handler(struct evhttp_request *req, void *arg)
{
  const char *ruri = evhttp_request_get_uri(req);

  if (verbose)
    fprintf(stderr, "new pub request URI '%s'\n", ruri);

  if (check_ip(req->remote_host) == 0) {
    fprintf(stderr, "not allowed '%s:%d'\n", req->remote_host, req->remote_port);

    evhttp_add_header(req->output_headers, "Connection", "Close");
    evhttp_send_reply(req, HTTP_FORBIDDEN, "Forbidden", NULL);
    stats.forbidden++;
    return;
  }

  if (evhttp_request_get_command(req) != EVHTTP_REQ_GET) {
    fprintf(stderr, "invalide method '%s:%d'\n", req->remote_host, req->remote_port);

    evhttp_send_reply(req, HTTP_BADMETHOD, "Invalid Method", NULL);
    stats.invalid_method++;
    return;
  }
  
  struct evbuffer *buf;

  struct evkeyvalq params;
  evhttp_parse_query(ruri, &params);

  const char *name = get_str(&params, "channel", NULL);
  if (name == NULL || strlen(name) == 0) {
    evhttp_send_reply(req, HTTP_BADREQUEST, "Invalid Request", NULL);

    evhttp_clear_headers(&params);
    stats.invalid_request++;
    return;
  }

  const char *message = get_str(&params, "message", NULL);
  if (message == NULL || strlen(message) == 0) {
    evhttp_send_reply(req, HTTP_BADREQUEST, "Invalid Request", NULL);

    evhttp_clear_headers(&params);
    stats.invalid_request++;
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
    evbuffer_add_printf(buf, "%s", message);
    if (connection->callback) {
      evbuffer_add_printf(buf, ");");
    }
    evbuffer_add_printf(buf, "\n");

    evhttp_send_reply_chunk(connection->req, buf);

    evbuffer_free(buf);

    close(connection);
    stats.notification++;
  }

  evhttp_add_header(req->output_headers, "Content-Type", "text/json; charset=utf-8");

  buf = evbuffer_new();
  evbuffer_add_printf(buf, "{sent: %d}\n", length);
  evhttp_send_reply(req, HTTP_OK, "OK", buf);
  evbuffer_free(buf);

  evhttp_clear_headers(&params);
  stats.publication++;
}

static void sub_handler(struct evhttp_request *req, void *arg)
{
  const char *ruri = evhttp_request_get_uri(req);

  if (verbose)
    fprintf(stderr, "new sub request URI '%s'\n", ruri);

  if (evhttp_request_get_command(req) != EVHTTP_REQ_GET) {
    evhttp_send_reply(req, HTTP_BADMETHOD, "Invalid Method", NULL);
    stats.invalid_method++;
    return;
  }

  struct evkeyvalq params;
  evhttp_parse_query(ruri, &params);

  const char *name = get_str(&params, "channel", NULL);
  if (name == NULL || strlen(name) == 0) {
    evhttp_send_reply(req, HTTP_BADREQUEST, "Invalid Request", NULL);
    stats.invalid_request++;
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

  stats.active_connection++;
  stats.connection++;
}

static void gen_handler(struct evhttp_request *req, void *arg)
{
  const char *ruri = evhttp_request_get_uri(req);

  if (verbose)
    fprintf(stderr, "unrecognized request URI '%s', sending 404\n", ruri);

  evhttp_send_reply(req, HTTP_BADREQUEST, "Bad Request", NULL);
  stats.unknown_request++;
}

static void sigint_cb(evutil_socket_t sig, short events, void *ptr)
{
  struct event_base *base = (event_base *)ptr;
  struct timeval delay = { 1, 0 };
 
  printf("Interrupted exiting...\n");
  event_base_loopexit(base, &delay);
}

static void sighup_cb(evutil_socket_t sig, short events, void *ptr)
{
  // TODO
}

int main (int argc, char *argv[])
{
  const char *address = "0.0.0.0";
  int port = 8080;
  struct event_base *base;
  struct evhttp *server;
  struct event *sigint_event, *sighup_event;
  int is_iplist = 0;

  TAILQ_INIT(&ips);

  int c;
  while ((c = getopt (argc, argv, "vha:p:i:")) != -1) {
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
      case 'i':
        parse_ips(optarg);
        is_iplist = 1;
        break;
      case 'h':
      default:
        fprintf(stderr, "usage: %s [-v] [-a 0.0.0.0] [-p 8080] [-i all]\n", argv[0]);
        exit(1);
    }
  }

  if (!is_iplist) {
    parse_ips(NULL);
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
  evhttp_set_cb(server, "/stat", stat_handler, NULL);
  evhttp_set_gencb(server, gen_handler, NULL);

  handle = evhttp_bind_socket_with_handle(server, address, port);

  fprintf(stderr, "Server bound to %s:%d\n", address, port);
  fprintf(stderr, "Entering dispatching loop, server started\n");

  event_base_dispatch(base);

  event_free(sigint_event);
  event_free(sighup_event);
  evhttp_free(server);
  event_base_free(base);

  return 0;
}

void parse_ips(char *iplist)
{
  char *token;
  struct in_addr ip_addr;
  struct ip *ip;
  int parsed = 0;
  
  token = strtok(iplist, ",");
  while( token != NULL ) 
  {
    if (strcmp(token, "all") == 0) {
      ip = (struct ip *) calloc(1, sizeof *ip);
      ip->ip = 0;
      TAILQ_INSERT_TAIL(&ips, ip, next);
    } else if (inet_aton(token, &ip_addr) != 0) {
      ip = (struct ip *) calloc(1, sizeof *ip);
      ip->ip = ip_addr.s_addr;
      TAILQ_INSERT_TAIL(&ips, ip, next);
    } else {
      printf("Uknkown ip definition: %s\n", token);
    }
    token = strtok(NULL, ",");
  }

  // fall back to add all
  if (TAILQ_EMPTY(&ips)) {
      ip = (struct ip *) calloc(1, sizeof *ip);
      ip->ip = 0;
      TAILQ_INSERT_TAIL(&ips, ip, next);
  }
}

int check_ip(const char *address)
{
  struct in_addr ip_addr;
  
  if (inet_aton(address, &ip_addr) == 0) {
    fprintf(stderr, "Unknown ip address format '%s'\n", address);
    return 0;
  }

  struct ip *ip;
  TAILQ_FOREACH(ip, &ips, next) {
    if (ip->ip == 0) {
      return 1;
    } else if (ip->ip == ip_addr.s_addr) {
      return 1;
    }
  }

  return 0;
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
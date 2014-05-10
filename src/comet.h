#include <stdint.h>
#include <sys/queue.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#define HTTP_FORBIDDEN 403

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
  uint32_t length;
  
  TAILQ_HEAD(,connection) connections;

  TAILQ_ENTRY(channel) next;
};

TAILQ_HEAD(,channel) channels;

struct ip {
  uint32_t ip;

  TAILQ_ENTRY(ip) next;
};

TAILQ_HEAD(,ip) ips;

struct stat {
  uint64_t publication;
  uint64_t notification;
  uint64_t connection;
  uint64_t active_connection;
  uint64_t invalid_method;
  uint64_t invalid_request;
  uint64_t unknown_request;
	uint64_t forbidden;
};

static void parse_ips(char *iplist);
int check_ip(const char *address);

int get_int(struct evkeyvalq *params, const char *name, int def);
const char* get_str(struct evkeyvalq *params, const char *name, const char *def);

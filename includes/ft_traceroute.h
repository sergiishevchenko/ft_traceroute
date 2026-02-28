#ifndef FT_TRACEROUTE_H
# define FT_TRACEROUTE_H

# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <unistd.h>
# include <stdbool.h>
# include <errno.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <sys/time.h>
# include <sys/select.h>
# include <netinet/in.h>
# include <netinet/ip.h>
# include <netinet/ip_icmp.h>
# include <netinet/udp.h>
# include <arpa/inet.h>
# include <netdb.h>

# define PROGRAM_NAME        "ft_traceroute"
# define DEFAULT_PORT        33434
# define DEFAULT_MAX_HOPS    30
# define DEFAULT_NPROBES     3
# define DEFAULT_PACKET_SIZE 60
# define DEFAULT_WAIT_TIME   5.0
# define PAYLOAD_SIZE        32
# define RECV_BUFF_SIZE      512
# define MAX_PROBES          10

# define FT_ICMP_UNREACH     3
# define FT_ICMP_TIMXCEED    11

# define FT_UNREACH_NET      0
# define FT_UNREACH_HOST     1
# define FT_UNREACH_PROTO    2
# define FT_UNREACH_PORT     3
# define FT_UNREACH_FRAG     4
# define FT_UNREACH_SR       5
# define FT_UNREACH_ADMIN9   9
# define FT_UNREACH_ADMIN10  10
# define FT_UNREACH_ADMIN13  13

typedef struct s_probe
{
	struct timeval	send_time;
	struct timeval	recv_time;
	double			rtt;
	char			addr_str[INET_ADDRSTRLEN];
	int				received;
	int				icmp_type;
	int				icmp_code;
	int				port;
}	t_probe;

typedef struct s_traceroute
{
	char				*host;
	char				resolved_ip[INET_ADDRSTRLEN];
	struct sockaddr_in	dest_addr;
	int					send_sock;
	int					recv_sock;
	int					port;
	int					max_hops;
	int					nprobes;
	int					packet_size;
	double				wait_time;
	int					ttl;
	int					seq;
	bool				reached;
	bool				no_dns;
	int					first_ttl;
}	t_traceroute;

void	parse_args(t_traceroute *tr, int argc, char **argv);
void	resolve_host(t_traceroute *tr);
void	create_sockets(t_traceroute *tr);
void	set_ttl(t_traceroute *tr, int ttl);
void	close_sockets(t_traceroute *tr);
void	send_probe(t_traceroute *tr, t_probe *probe);
int		recv_probe(t_traceroute *tr, t_probe *probe);
double	calc_rtt(struct timeval *send_tv, struct timeval *recv_tv);
void	print_header(t_traceroute *tr);
void	print_hop(t_traceroute *tr, t_probe *probes);
void	fatal_error(const char *msg);

#endif

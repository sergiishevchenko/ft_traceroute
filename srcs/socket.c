#include "ft_traceroute.h"

void	create_sockets(t_traceroute *tr)
{
	tr->recv_sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (tr->recv_sock < 0)
		fatal_error("cannot create ICMP socket");
	tr->send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (tr->send_sock < 0)
		fatal_error("cannot create UDP socket");
}

void	set_ttl(t_traceroute *tr, int ttl)
{
	if (setsockopt(tr->send_sock, IPPROTO_IP, IP_TTL,
			&ttl, sizeof(ttl)) < 0)
		fatal_error("setsockopt IP_TTL");
}

void	close_sockets(t_traceroute *tr)
{
	if (tr->recv_sock >= 0)
		close(tr->recv_sock);
	if (tr->send_sock >= 0)
		close(tr->send_sock);
}

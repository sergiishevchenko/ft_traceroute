#include "ft_traceroute.h"

static int	parse_icmp(char *buf, ssize_t len, int expected_port,
		t_probe *probe)
{
	int			outer_ihl;
	uint8_t		icmp_type;
	uint8_t		icmp_code;
	int			inner_offset;
	int			inner_ihl;
	uint8_t		inner_proto;
	int			udp_offset;
	uint16_t	udp_dport;

	outer_ihl = (buf[0] & 0x0f) * 4;
	if (len < outer_ihl + 8 + 20 + 8)
		return (-1);
	icmp_type = (uint8_t)buf[outer_ihl];
	icmp_code = (uint8_t)buf[outer_ihl + 1];
	if (icmp_type != FT_ICMP_TIMXCEED && icmp_type != FT_ICMP_UNREACH)
		return (-1);
	inner_offset = outer_ihl + 8;
	inner_ihl = (buf[inner_offset] & 0x0f) * 4;
	inner_proto = (uint8_t)buf[inner_offset + 9];
	if (inner_proto != IPPROTO_UDP)
		return (-1);
	udp_offset = inner_offset + inner_ihl;
	if (len < udp_offset + 4)
		return (-1);
	memcpy(&udp_dport, &buf[udp_offset + 2], sizeof(udp_dport));
	udp_dport = ntohs(udp_dport);
	if (udp_dport != (uint16_t)expected_port)
		return (-1);
	probe->icmp_type = icmp_type;
	probe->icmp_code = icmp_code;
	return (0);
}

int	recv_probe(t_traceroute *tr, t_probe *probe)
{
	char				buf[RECV_BUFF_SIZE];
	struct sockaddr_in	from;
	socklen_t			from_len;
	fd_set				fds;
	struct timeval		tv;
	struct timeval		start;
	struct timeval		now;
	double				remaining;
	ssize_t				bytes;
	int					ret;

	gettimeofday(&start, NULL);
	while (1)
	{
		gettimeofday(&now, NULL);
		remaining = tr->wait_time
			- ((now.tv_sec - start.tv_sec)
				+ (now.tv_usec - start.tv_usec) / 1000000.0);
		if (remaining <= 0)
			return (probe->received = 0);
		tv.tv_sec = (long)remaining;
		tv.tv_usec = (long)((remaining - (double)tv.tv_sec) * 1000000.0);
		FD_ZERO(&fds);
		FD_SET(tr->recv_sock, &fds);
		ret = select(tr->recv_sock + 1, &fds, NULL, NULL, &tv);
		if (ret <= 0)
			return (probe->received = 0);
		from_len = sizeof(from);
		bytes = recvfrom(tr->recv_sock, buf, sizeof(buf), 0,
				(struct sockaddr *)&from, &from_len);
		if (bytes < 0)
			continue ;
		gettimeofday(&probe->recv_time, NULL);
		if (parse_icmp(buf, bytes, probe->port, probe) == 0)
		{
			snprintf(probe->addr_str, sizeof(probe->addr_str), "%s",
				inet_ntoa(from.sin_addr));
			probe->received = 1;
			probe->rtt = calc_rtt(&probe->send_time, &probe->recv_time);
			return (1);
		}
	}
}

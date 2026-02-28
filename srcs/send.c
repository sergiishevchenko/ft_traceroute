#include "ft_traceroute.h"

void	send_probe(t_traceroute *tr, t_probe *probe)
{
	struct sockaddr_in	dest;
	char				payload[PAYLOAD_SIZE];

	probe->port = tr->port + tr->seq;
	memset(payload, 0, sizeof(payload));
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr = tr->dest_addr.sin_addr;
	dest.sin_port = htons(probe->port);
	gettimeofday(&probe->send_time, NULL);
	if (sendto(tr->send_sock, payload, sizeof(payload), 0,
			(struct sockaddr *)&dest, sizeof(dest)) < 0)
	{
		fprintf(stderr, "%s: sendto: %s\n", PROGRAM_NAME, strerror(errno));
	}
	tr->seq++;
}

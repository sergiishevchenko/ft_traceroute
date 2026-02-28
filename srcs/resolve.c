#include "ft_traceroute.h"

void	resolve_host(t_traceroute *tr)
{
	struct addrinfo	hints;
	struct addrinfo	*res;
	int				ret;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	ret = getaddrinfo(tr->host, NULL, &hints, &res);
	if (ret != 0)
	{
		fprintf(stderr, "%s: %s: %s\n", PROGRAM_NAME, tr->host,
			gai_strerror(ret));
		exit(EXIT_FAILURE);
	}
	memcpy(&tr->dest_addr, res->ai_addr, sizeof(tr->dest_addr));
	snprintf(tr->resolved_ip, sizeof(tr->resolved_ip), "%s",
		inet_ntoa(tr->dest_addr.sin_addr));
	freeaddrinfo(res);
}

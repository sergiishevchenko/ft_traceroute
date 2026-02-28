#include "ft_traceroute.h"

void	print_header(t_traceroute *tr)
{
	printf("traceroute to %s (%s), %d hops max, %d byte packets\n",
		tr->host, tr->resolved_ip, tr->max_hops, tr->packet_size);
}

static void	print_annotation(int icmp_type, int icmp_code)
{
	if (icmp_type != FT_ICMP_UNREACH)
		return ;
	if (icmp_code == FT_UNREACH_NET)
		printf(" !N");
	else if (icmp_code == FT_UNREACH_HOST)
		printf(" !H");
	else if (icmp_code == FT_UNREACH_PROTO)
		printf(" !P");
	else if (icmp_code == FT_UNREACH_FRAG)
		printf(" !F");
	else if (icmp_code == FT_UNREACH_SR)
		printf(" !S");
	else if (icmp_code == FT_UNREACH_ADMIN9
		|| icmp_code == FT_UNREACH_ADMIN10
		|| icmp_code == FT_UNREACH_ADMIN13)
		printf(" !X");
	else if (icmp_code != FT_UNREACH_PORT)
		printf(" !<%d>", icmp_code);
}

static void	print_addr(const char *ip_str, bool no_dns)
{
	struct sockaddr_in	sa;
	char				hostname[NI_MAXHOST];

	if (no_dns)
	{
		printf(" %s", ip_str);
		return ;
	}
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	inet_pton(AF_INET, ip_str, &sa.sin_addr);
	if (getnameinfo((struct sockaddr *)&sa, sizeof(sa),
			hostname, sizeof(hostname), NULL, 0, 0) == 0)
		printf(" %s (%s)", hostname, ip_str);
	else
		printf(" %s (%s)", ip_str, ip_str);
}

void	print_hop(t_traceroute *tr, t_probe *probes)
{
	char	last_addr[INET_ADDRSTRLEN];
	int		i;

	printf("%2d ", tr->ttl);
	last_addr[0] = '\0';
	i = 0;
	while (i < tr->nprobes)
	{
		if (!probes[i].received)
		{
			printf(" *");
			i++;
			continue ;
		}
		if (strcmp(probes[i].addr_str, last_addr) != 0)
		{
			print_addr(probes[i].addr_str, tr->no_dns);
			snprintf(last_addr, sizeof(last_addr), "%s", probes[i].addr_str);
		}
		printf("  %.3f ms", probes[i].rtt);
		print_annotation(probes[i].icmp_type, probes[i].icmp_code);
		i++;
	}
	printf("\n");
}

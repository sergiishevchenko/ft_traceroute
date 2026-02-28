#include "ft_traceroute.h"

static void	init_traceroute(t_traceroute *tr)
{
	memset(tr, 0, sizeof(t_traceroute));
	tr->port = DEFAULT_PORT;
	tr->max_hops = DEFAULT_MAX_HOPS;
	tr->nprobes = DEFAULT_NPROBES;
	tr->packet_size = DEFAULT_PACKET_SIZE;
	tr->wait_time = DEFAULT_WAIT_TIME;
	tr->send_sock = -1;
	tr->recv_sock = -1;
	tr->seq = 0;
	tr->reached = false;
	tr->no_dns = false;
	tr->first_ttl = 1;
}

static bool	check_reached(t_probe *probes, int nprobes)
{
	int	i;

	i = 0;
	while (i < nprobes)
	{
		if (probes[i].received
			&& probes[i].icmp_type == FT_ICMP_UNREACH
			&& probes[i].icmp_code == FT_UNREACH_PORT)
			return (true);
		i++;
	}
	return (false);
}

int	main(int argc, char **argv)
{
	t_traceroute	tr;
	t_probe			probes[MAX_PROBES];
	int				p;

	init_traceroute(&tr);
	parse_args(&tr, argc, argv);
	resolve_host(&tr);
	create_sockets(&tr);
	print_header(&tr);
	tr.ttl = tr.first_ttl;
	while (tr.ttl <= tr.max_hops)
	{
		set_ttl(&tr, tr.ttl);
		memset(probes, 0, sizeof(t_probe) * tr.nprobes);
		p = 0;
		while (p < tr.nprobes)
		{
			send_probe(&tr, &probes[p]);
			recv_probe(&tr, &probes[p]);
			p++;
		}
		print_hop(&tr, probes);
		if (check_reached(probes, tr.nprobes))
			break ;
		tr.ttl++;
	}
	close_sockets(&tr);
	return (0);
}

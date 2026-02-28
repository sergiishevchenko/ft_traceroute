#include "ft_traceroute.h"

double	calc_rtt(struct timeval *send_tv, struct timeval *recv_tv)
{
	return ((recv_tv->tv_sec - send_tv->tv_sec) * 1000.0
		+ (recv_tv->tv_usec - send_tv->tv_usec) / 1000.0);
}

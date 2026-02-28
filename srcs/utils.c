#include "ft_traceroute.h"

void	fatal_error(const char *msg)
{
	fprintf(stderr, "%s: %s: %s\n", PROGRAM_NAME, msg, strerror(errno));
	exit(EXIT_FAILURE);
}

#include "ft_traceroute.h"

static void	print_help(void)
{
	printf("Usage:\n");
	printf("  %s [-n] [-m max_ttl] [-q nqueries]", PROGRAM_NAME);
	printf(" [-p port] [-f first_ttl] host\n\n");
	printf("Arguments:\n");
	printf("  host          The host to traceroute to\n\n");
	printf("Options:\n");
	printf("  --help        Read this help and exit\n");
	printf("  -n            Do not resolve IP addresses to hostnames\n");
	printf("  -f first_ttl  Start from the first_ttl hop (default 1)\n");
	printf("  -m max_ttl    Set the max number of hops (default 30)\n");
	printf("  -q nqueries   Set the number of probes per hop (default 3)\n");
	printf("  -p port       Set the destination port base (default 33434)\n");
}

static int	ft_atoi_safe(const char *str)
{
	long	result;
	int		i;

	result = 0;
	i = 0;
	while (str[i])
	{
		if (str[i] < '0' || str[i] > '9')
			return (-1);
		result = result * 10 + (str[i] - '0');
		if (result > 2147483647)
			return (-1);
		i++;
	}
	if (i == 0)
		return (-1);
	return ((int)result);
}

static int	parse_optval(char **argv, int *i, const char *name,
		int min, int max)
{
	int	val;

	(*i)++;
	if (!argv[*i])
	{
		fprintf(stderr, "%s: option requires an argument -- '%s'\n",
			PROGRAM_NAME, name);
		exit(EXIT_FAILURE);
	}
	val = ft_atoi_safe(argv[*i]);
	if (val < min || val > max)
	{
		fprintf(stderr, "%s: invalid value for `%s': `%s' ",
			PROGRAM_NAME, name, argv[*i]);
		fprintf(stderr, "(must be %d-%d)\n", min, max);
		exit(EXIT_FAILURE);
	}
	return (val);
}

static void	parse_flag(t_traceroute *tr, char **argv, int *i)
{
	if (strcmp(argv[*i], "--help") == 0)
	{
		print_help();
		exit(EXIT_SUCCESS);
	}
	else if (strcmp(argv[*i], "-n") == 0)
		tr->no_dns = true;
	else if (strcmp(argv[*i], "-f") == 0)
		tr->first_ttl = parse_optval(argv, i, "first_ttl", 1, 255);
	else if (strcmp(argv[*i], "-m") == 0)
		tr->max_hops = parse_optval(argv, i, "max_ttl", 1, 255);
	else if (strcmp(argv[*i], "-q") == 0)
		tr->nprobes = parse_optval(argv, i, "nqueries", 1, MAX_PROBES);
	else if (strcmp(argv[*i], "-p") == 0)
		tr->port = parse_optval(argv, i, "port", 1, 65535);
	else
	{
		fprintf(stderr, "%s: bad option `%s'\n", PROGRAM_NAME, argv[*i]);
		fprintf(stderr, "Try '%s --help' for more information.\n",
			PROGRAM_NAME);
		exit(EXIT_FAILURE);
	}
}

void	parse_args(t_traceroute *tr, int argc, char **argv)
{
	int	i;

	if (argc < 2)
	{
		fprintf(stderr, "%s: missing host operand\n", PROGRAM_NAME);
		fprintf(stderr, "Try '%s --help' for more information.\n",
			PROGRAM_NAME);
		exit(EXIT_FAILURE);
	}
	i = 1;
	while (i < argc)
	{
		if (argv[i][0] == '-')
			parse_flag(tr, argv, &i);
		else if (tr->host != NULL)
		{
			fprintf(stderr, "%s: too many arguments\n", PROGRAM_NAME);
			exit(EXIT_FAILURE);
		}
		else
			tr->host = argv[i];
		i++;
	}
	if (tr->host == NULL)
	{
		fprintf(stderr, "%s: missing host operand\n", PROGRAM_NAME);
		exit(EXIT_FAILURE);
	}
	if (getuid() != 0)
	{
		fprintf(stderr, "%s: root privileges required\n", PROGRAM_NAME);
		exit(EXIT_FAILURE);
	}
}

NAME		= ft_traceroute

CC			= gcc
CFLAGS		= -Wall -Wextra -Werror

SRC_DIR		= srcs
INC_DIR		= includes
OBJ_DIR		= objs

SRCS		= main.c args.c resolve.c socket.c send.c recv.c time.c \
			  display.c utils.c

OBJS		= $(addprefix $(OBJ_DIR)/, $(SRCS:.c=.o))

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(NAME)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(INC_DIR)/ft_traceroute.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re

NAME		= ft_traceroute

CC			= gcc
CFLAGS		= -Wall -Wextra -Werror

GREEN		= \033[1;32m
YELLOW		= \033[1;33m
BLUE		= \033[1;34m
RESET		= \033[0m

SRC_DIR		= srcs
INC_DIR		= includes
OBJ_DIR		= objs

SRCS		= main.c args.c resolve.c socket.c send.c recv.c time.c \
			  display.c utils.c

OBJS		= $(addprefix $(OBJ_DIR)/, $(SRCS:.c=.o))

all: $(NAME)

$(NAME): $(OBJS)
	@printf "$(YELLOW)Linking$(RESET) %s\n" $@
	@$(CC) $(CFLAGS) $(OBJS) -o $(NAME)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(INC_DIR)/ft_traceroute.h | $(OBJ_DIR)
	@printf "$(BLUE)Compiling$(RESET) %s\n" $<
	@$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

$(OBJ_DIR):
	@printf "$(GREEN)Creating$(RESET) %s\n" $@
	@mkdir -p $(OBJ_DIR)

clean:
	@printf "$(GREEN)Cleaning$(RESET) %s\n" $(OBJ_DIR)
	@rm -rf $(OBJ_DIR)

fclean: clean
	@printf "$(GREEN)Removing$(RESET) %s\n" $(NAME)
	@rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re

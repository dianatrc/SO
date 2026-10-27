// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1

char current_directory[1024];
/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	/* TODO: Execute cd. */
	char *path = NULL;
	bool free_needed = false;

	// no arguments
	if (dir == NULL) {
		path = getenv("HOME");
		if (path == NULL)
			return 1;
	} else {
		// multiple arguments
		if (dir->next_word != NULL)
			return 1;

		path = get_word(dir);
		free_needed = true;
	}

	if (chdir(path) == -1) {
		fprintf(stderr, "cd: no such file or directory\n");
		if (free_needed)
			free(path);
		return 1;
	}

	if (getcwd(current_directory, sizeof(current_directory)) != NULL)
		setenv("PWD", current_directory, 1);

	if (free_needed)
		free(path);

	return 0;
}

// Internal pwd command
static void shell_pwd(void)
{
	getcwd(current_directory, sizeof(current_directory));
	printf("%s\n", current_directory);
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	/* TODO: Execute exit/quit. */

	return SHELL_EXIT; /* TODO: Replace with actual exit code. */
}

// Handle variable assignment (VAR=value)
static int variable_assignment(simple_command_t *s)
{
	// get the full assigment string
	char *assignment = get_word(s->verb);

	// save character '=' position
	char *equals_pos = strchr(assignment, '=');

	if (!equals_pos) {
		// no '=' character
		free(assignment);
		return 1;
	}

	*equals_pos = '\0';
	char *var_name = assignment;

	char *var_value = equals_pos + 1;

	char *value_to_set = var_value;

	// handle value from params if right side is empty
	if (*var_value == '\0' && s->params)
		value_to_set = get_word(s->params);

	setenv(var_name, value_to_set, 1);

	if (value_to_set != var_value)
		free(value_to_set);

	free(assignment);
	return 0;
}

// Redirections
static int redirections(simple_command_t *s)
{
	int fd;
	char *filename;

	// input redirection: < filename
	if (s->in) {
		filename = get_word(s->in);
		fd = open(filename, O_RDONLY);

		if (fd == -1) {
			fprintf(stderr, "Execution failed for '%s'\n", filename);
			free(filename);
			return -1;
		}

		// redirect stdin
		dup2(fd, 0); // 0 fd for stdin
		close(fd);
		free(filename);
	}
	// output error redirection: &> filename
	if (s->out && s->err) {
		char *out_filename = get_word(s->out);

		char *err_filename = get_word(s->err);

		if (strcmp(out_filename, err_filename) == 0) {
			if (s->io_flags & IO_OUT_APPEND)
				fd = open(out_filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
			else
				fd = open(out_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);

			if (fd == -1) {
				fprintf(stderr, "Execution failed for '%s'\n", filename);
				free(out_filename);
				free(err_filename);
				return -1;
			}

			// redirect stdout, stderr
			dup2(fd, 1); // 1 fd for stdout
			dup2(fd, 2); // 2 fd for stderr
			close(fd);
			free(out_filename);
			free(err_filename);
			return 0;
		}

		free(out_filename);
		free(err_filename);
	}
	// output redirection: > filename or >> filename
	if (s->out) {
		filename = get_word(s->out);

		if (s->io_flags & IO_OUT_APPEND)
			fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
		else
			fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);

		if (fd == -1) {
			fprintf(stderr, "Execution failed for '%s'\n", filename);
			free(filename);
			return -1;
		}

		// redirect stdout
		dup2(fd, 1); // 1 fd for stdout
		close(fd);
		free(filename);
	}
	// error redirection: 2> filename or 2>> filename
	if (s->err) {
		filename = get_word(s->err);
		if (s->io_flags & IO_ERR_APPEND)
			fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0644);
		else
			fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);

		if (fd == -1) {
			fprintf(stderr, "Execution failed for '%s'\n", filename);
			free(filename);
			return -1;
		}

		// redirect stderr
		dup2(fd, 2); // 2 fd for stderr
		close(fd);
		free(filename);
	}
	return 0;
}

static bool is_valid_assigment(const char *str)
{
	// check if format is valid
	if (!str || *str == '\0')
		return false;

	// start with letter or underscore
	if (!isalpha(str[0]) && str[0] != '_')
		return false;

	char *equal = strchr(str, '=');

	// no '=' character
	if (!equal || equal == str)
		return false;

	// all characters valid
	for (const char *p = str; p < equal; p++)
		if (!isalnum(*p) && *p != '_')
			return false;

	return true;
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	/* TODO: Sanity checks. */
	if (!s || !s->verb)
		return 0;

	/* TODO: If builtin command, execute the command. */
	char *cmd = get_word(s->verb);
	int status = -1;

	if (strcmp(cmd, "cd") == 0) {
		int saved_in = dup(0);
		int saved_out = dup(1);
		int saved_err = dup(2);

		if (redirections(s) != -1)
			status = shell_cd(s->params);

		dup2(saved_in, 0);
		dup2(saved_out, 1);
		dup2(saved_err, 2);

		close(saved_in);
		close(saved_out);
		close(saved_err);
	} else if (strcmp(cmd, "pwd") == 0) {
		int saved_stdout = dup(1);

		if (redirections(s) == -1) {
			free(cmd);
			return 1;
		}
		shell_pwd();

		dup2(saved_stdout, 1);
		close(saved_stdout);

		status = 0;
	} else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
		status = shell_exit();
	}
	if (status != -1) {
		free(cmd);
		return status;
	}
	/* TODO: If variable assignment, execute the assignment and return
	 * the exit status.
	 */
	if (strchr(cmd, '=') != NULL && is_valid_assigment(cmd)) {
		free(cmd);
		return variable_assignment(s);
	}
	/* TODO: If external command:
	 *   1. Fork new process
	 *     2c. Perform redirections in child
	 *     3c. Load executable in child
	 *   2. Wait for child
	 *   3. Return exit status
	 */
	pid_t pid = fork();

	if (pid == -1) {
		perror("fork");
		free(cmd);
		return 1;
	}

	if (pid == 0) {
		// child process
		if (redirections(s) == -1) {
			free(cmd);
			_exit(1);
		}

		// load executable
		int argc;
		char **argv = get_argv(s, &argc);

		if (!argv || !argv[0]) {
			if (argv)
				free(argv);
			free(cmd);
			_exit(1);
		}

		execvp(argv[0], argv);

		fprintf(stderr, "Execution failed for '%s'\n", argv[0]);
		for (int i = 0; i < argc; i++)
			free(argv[i]);
		free(argv);
		free(cmd);

		_exit(127);
	}
	//parent process
	waitpid(pid, &status, 0);
	free(cmd);

	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	return 1; /* TODO: Replace with actual exit status. */
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* TODO: Execute cmd1 and cmd2 simultaneously. */
	pid_t pid1, pid2;

	int status1, status2;

	pid1 = fork();
	if (pid1 == -1) {
		perror("fork");
		return 1;
	}
	if (pid1 == 0) {
		// first child process
		_exit(parse_command(cmd1, level + 1, father));
	}

	pid2 = fork();
	if (pid2 == -1) {
		perror("fork");
		waitpid(pid1, &status1, 0);  // wait for the first child
		return 1;
	}
	if (pid2 == 0) {
		// second child
		_exit(parse_command(cmd2, level + 1, father));
	}

	// parent waits for both children
	waitpid(pid1, &status1, 0);
	waitpid(pid2, &status2, 0);

	// return exit status of second command
	if (WIFEXITED(status2))
		return WEXITSTATUS(status2);

	return 1; /* TODO: Replace with actual exit status. */
}

static void launch_pipe_child(command_t *c, int pipe_fd, int target_fd,
								int unused_fd, int level, command_t *father)
{
	close(unused_fd);
	dup2(pipe_fd, target_fd);
	close(pipe_fd);

	_exit(parse_command(c, level + 1, father));
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* TODO: Redirect the output of cmd1 to the input of cmd2. */
	int pipe_fd[2];

	pid_t pid1, pid2;

	// create pipe
	if (pipe(pipe_fd) == -1) {
		perror("pipe failure");
		return 1;
	}

	pid1 = fork();
	if (pid1 == 0) {
		// first child writes to pipe
		launch_pipe_child(cmd1, pipe_fd[WRITE], 1, pipe_fd[READ], level, father);
	} else if (pid1 < 0) {
		// fork failed
		perror("fork1");
		close(pipe_fd[0]);  // 0 for read
		close(pipe_fd[1]); // 1 for write
		return 1;
	}

	pid2 = fork();
	if (pid2 == 0) {
		// second child reads from pipe
		launch_pipe_child(cmd2, pipe_fd[READ], 0, pipe_fd[WRITE], level, father);
	} else if (pid2 < 0) {
		// fork failed
		perror("fork2");
		close(pipe_fd[0]);
		close(pipe_fd[1]);
		waitpid(pid1, NULL, 0);
		return 1;
	}

	close(pipe_fd[0]);
	close(pipe_fd[1]);

	int status1, status2;

	// parent waits for both children
	waitpid(pid1, &status1, 0);
	waitpid(pid2, &status2, 0);

	// return exit status of second command
	if (WIFEXITED(status2) && WEXITSTATUS(status2) == 0)
		return 0;

	return 1; /* TODO: Replace with actual exit status. */
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	/* TODO: sanity checks */
	if (c == NULL)
		return 0;

	if (c->op == OP_NONE) {
		/* TODO: Execute a simple command. */
		return parse_simple(c->scmd, level, father);
	}

	int status;

	switch (c->op) {
	case OP_SEQUENTIAL:
		/* TODO: Execute the commands one after the other. */
		parse_command(c->cmd1, level + 1, c);
		return parse_command(c->cmd2, level + 1, c);

	case OP_PARALLEL:
		/* TODO: Execute the commands simultaneously. */
		return run_in_parallel(c->cmd1, c->cmd2, level, c);

	case OP_CONDITIONAL_NZERO:
		/* TODO: Execute the second command only if the first one
		 * returns non zero.
		 */
		status = parse_command(c->cmd1, level + 1, c);
		if (status != 0)
			return parse_command(c->cmd2, level + 1, c);
		return status;

	case OP_CONDITIONAL_ZERO:
		/* TODO: Execute the second command only if the first one
		 * returns zero.
		 */
		status = parse_command(c->cmd1, level + 1, c);
		if (status == 0)
			return parse_command(c->cmd2, level + 1, c);
		return status;

	case OP_PIPE:
		/* TODO: Redirect the output of the first command to the
		 * input of the second.
		 */
		return run_on_pipe(c->cmd1, c->cmd2, level, c);

	default:
		return SHELL_EXIT;
	}
}

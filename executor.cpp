#include "executor.hpp"
#include <stdlib.h>

#ifdef OS_WIN
    #include <windows.h>
#elif _POSIX_C_SOURCE >= 199309L
    #include <sys/time.h>
    #include <time.h>
#else
    #include <sys/time.h>
#endif

using namespace CommandExecutor;

long long current_time_millis() {
#ifdef WIN32
	FILETIME time;
	GetSystemTimeAsFileTime(&time);
	return (long long)(((LONGLONG)time.dwLowDateTime + ((LONGLONG)(time.dwHighDateTime) << 32LL) - 116444736000000000LL) / 10000LL);
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
	return (long long)((unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000);
#endif
}

Command *Command::then(Command *cmd, long delay) {
	if (cmd == NULL) return cmd;
	_next = cmd;
	_next_scheduled_millis = delay;
	return cmd;
}

Command *Command::next() {
	return _next;
}

long Command::next_delay() {
	return _next_scheduled_millis;
}

long Command::time_passed() {
	return current_time_millis() - _time_launched;
}

void Command::timeout(long millis) {
	_timeout = millis;
}

long Command::timeout() {
	return _timeout;
}

Executor *Command::exec() {
	return _exec;
}

void Command::prelaunch(long time, Executor *exec) {
	_time_launched = time;
	_exec = exec;
}

Executor::Executor(int start_size, int realloc_size) : mtx() {
	_realloc_size = realloc_size;

	command_stack = (StackMember *)malloc(start_size * sizeof(StackMember));
	memset(command_stack, 0, start_size * sizeof(StackMember));
	command_stack_size = start_size;

	op_stack = (StackMember *)malloc(start_size * sizeof(StackMember));
	memset(op_stack, 0, start_size * sizeof(StackMember));
	op_stack_size = realloc_size;
}

void Executor::push(Command *cmd, long delay, bool locking) {
	if (cmd == NULL) return;
	if (locking) mtx.lock();
	StackMember member = { true, OP_PUSH, cmd, current_time_millis(), delay };
	op_stack[create_stack_index(cmd, &op_stack, &op_stack_size)] = member;
	if (locking) mtx.unlock();
}

void Executor::push_unsafe(Command *cmd) {
	long long now = current_time_millis();
	if (!cmd->can_start()) {
		delete cmd;
		return;
	}
	cmd->prelaunch(now, this);
	cmd->start();
	StackMember member = { true, OP_CMD_STACK, cmd, now, cmd->timeout() };
	command_stack[create_stack_index(cmd, &command_stack, &command_stack_size)] = member;
}

void Executor::pop(Command *cmd) {
	if (cmd == NULL) return;
	mtx.lock();
	StackMember member = { true, OP_POP, cmd, current_time_millis(), 0 };
	op_stack[create_stack_index(cmd, &op_stack, &op_stack_size)] = member;
	mtx.unlock();
}

void Executor::pop_unsafe(Command *cmd) {
	int idx = find_stack_index(cmd, &command_stack, &command_stack_size);
	if (idx != -1) {
		command_stack[idx].exists = false;
		cmd->stop();

		if (cmd->next() != NULL) {
			if (cmd->next_delay() == 0L)
				push_unsafe(cmd->next());
			else
				push(cmd->next(), cmd->next_delay(), false);
		}

		delete cmd;
	}
}

void Executor::tick() {
	for (int i = 0; i < command_stack_size; i++) {
		StackMember *m = &command_stack[i];
		if (m->exists && m->cmd != NULL) {
			Command *cmd = m->cmd;
			long long now = current_time_millis();

			if (m->timeout_duration != 0L && now >= (m->timeout_start + m->timeout_duration)) {
				pop(cmd);
			} else {
				cmd->periodic();
				if (cmd->complete()) {
					pop(cmd);
				}
			}
		}
	}

	mtx.lock();
	for (int i = 0; i < op_stack_size; i++) {
		StackMember *m = &op_stack[i];
		if (m->exists && m->cmd != NULL) {
			long long now = current_time_millis();
			if (m->operation == OP_PUSH) {
				if (m->timeout_duration == 0) {
					push_unsafe(m->cmd);
					m->exists = false;
				} else {
					if (now >= (m->timeout_duration + m->timeout_start)) {
						push_unsafe(m->cmd);
						m->exists = false;
					}
				}
			} else if (m->operation == OP_POP) {
				pop_unsafe(m->cmd);
				m->exists = false;
			} else {
				m->exists = false;
			}
		}
	}
	mtx.unlock();
}

int Executor::find_stack_index(Command *cmd, StackMember **stack, int *stack_size) {
	for (int i = 0; i < *stack_size; i++) {
		if ((*stack)[i].exists && (*stack)[i].cmd == cmd) return i;
	}
	return -1;
}

int Executor::create_stack_index(Command *cmd, StackMember **stack, int *stack_size) {
	for (int i = 0; i < *stack_size; i++) {
		if (!(*stack)[i].exists) return i;
	}

	int old_size = *stack_size;
	*stack_size += _realloc_size;
	StackMember *tmpstack = (StackMember *)malloc(*stack_size * sizeof(StackMember));
	memset(tmpstack, 0, sizeof(StackMember) * *stack_size);
	memcpy(tmpstack, *stack, old_size * sizeof(StackMember));
	*stack = tmpstack;
	return old_size;
}
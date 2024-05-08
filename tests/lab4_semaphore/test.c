#include <lib.h>

static void os_assert(int cond, const char *err) {
	if (!cond) {
		user_halt("%s\n", err);
	}
}

int main() {
	debugf("yes 1\n");
	sem_open(1, 1);
	debugf("yes 2\n");

	debugf("3\n");
	os_assert(sem_wait(2) != 0, "ERROR: RETURNING 0 WHEN SEM DOES NOT EXIST");
	debugf("4\n");
	os_assert(sem_wait(2) == -14, "ERROR: NOT RETURNING CORRECT ERROR CODE");
	os_assert(sem_wait(1) == 0, "ERROR: NOT RETURNING 0 WHEN CORRECT WAIT");

	os_assert(sem_post(1) == 0, "ERROR: NOT RETURNING 0 WHEN CORRECT POST");
	os_assert(sem_wait(1) == 0, "ERROR: NOT RETURNING 0 WHEN WAIT AFTER POST");

	os_assert(sem_kill(1) == 0, "ERROR: NOT RETURNING 0 WHEN CORRECT KILL");
	os_assert(sem_wait(1) == -14, "ERROR: BAD RETURN WHEN ACCESS KILLED SEM");

	debugf("OSTEST_OK\n");
	return 0;
}

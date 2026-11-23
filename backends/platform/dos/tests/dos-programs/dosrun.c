/* DOS test helper: run a program with its error output (what a DJGPP program says when it
 * dies, "Exiting due to signal SIGSEGV" and the register dump) going to the same place as its
 * normal output, so that "DOSRUN.EXE PROGRAM.EXE > OUT.TXT" catches both: COMMAND.COM cannot
 * redirect the error output on its own.
 *
 * Usage: DOSRUN.EXE <program> [arguments]
 * Prints "DOSRUN: exit status <n>" last.
 */
#include <dpmi.h>
#include <go32.h>
#include <process.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
	int status;

	if (argc < 2) {
		printf("usage: DOSRUN.EXE <program> [arguments]\n");
		return 2;
	}

	setvbuf(stdout, NULL, _IONBF, 0);
	dup2(1, 2);
	/* what the program run through here finds free: the baseline for how much it uses (tests/check-plugin-ram.sh) */
	printf("DOSRUN: free memory at start: %lu KB\n", (unsigned long)(_go32_dpmi_remaining_physical_memory() / 1024));
	status = spawnvp(P_WAIT, argv[1], (char *const *)&argv[1]);
	printf("DOSRUN: exit status %d\n", status);
	return status;
}

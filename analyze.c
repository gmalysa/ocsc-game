#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char seq[8192];
static char strbuf[sizeof(seq)+1];

size_t get_count(char m, size_t len) {
	size_t count = 0;
	for (size_t i = 0; i < len; ++i) {
		if (seq[i] == m)
			count += 1;
	}

	return count;
}

void print_stats(size_t len) {
	printf("Stats:\n");
	printf(" A: %zu\n", get_count('A', len));
	printf(" a: %zu\n", get_count('a', len));
	printf(" B: %zu\n", get_count('B', len));
	printf(" b: %zu\n", get_count('b', len));
	printf(" C: %zu\n", get_count('C', len));
	printf(" c: %zu\n", get_count('c', len));
	printf(" D: %zu\n", get_count('D', len));
	printf(" d: %zu\n", get_count('d', len));
}

int main(int argc, char **argv) {
	size_t len;
	size_t i, j;
	bool search;
	ssize_t rlen;

	rlen = read(STDIN_FILENO, seq, sizeof(seq));
	if (rlen >= (ssize_t) sizeof(seq)) {
		printf("input was much longer than expected, recompile with bigger seq len\n");
		exit(1);
	}

	len = (size_t) rlen;
	printf("Read string:\n");
	printf("  length: %zd\n", len);
	printf("  rejected: %zd\n", len - 1000);
	print_stats(len);

	// Save original to print first
	memcpy(strbuf, seq, len);
	strbuf[len] = '\0';

	// Working backwards, find a suitable movement for the last person accepted until none
	// exist
	search = true;
	i = len-1;
	while (search) {
next:
		// Someone who would not have been seen/rejected with the improved sequence
		while (i > 0 && islower(seq[i]))
			i -= 1;

		if (i > 0 && isupper(seq[i])) {
			for (j = 0; j < i; ++j) {
				// Swap with a previously rejected person of the same type
				if (seq[j] == tolower(seq[i])) {
					char t = seq[i];
					seq[i] = seq[j];
					seq[j] = t;
					goto next;
				}
			}
		}

		search = false;
	}

	// subtracted 1 from i to get end of string index, convert it back to length
	i += 1;

	printf("Improved string:\n");
	printf("  length: %zu\n", i);
	printf("  rejected: %zu\n", i - 1000);
	printf("  improvement: %zd\n", len - i);
	print_stats(i);

	printf("In: %s\n", strbuf);

	memcpy(strbuf, seq, i);
	strbuf[i] = '\0';
	printf("Out: %s\n", strbuf);

	return 0;
}

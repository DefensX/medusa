
#include <stdlib.h>
#include <string.h>

__attribute__ ((visibility ("default"))) char * medusa_strndup (const char *str, size_t n)
{
	size_t len;
	char *copy;
	for (len = 0; len < n && str[len]; len++) {
		continue;
        }
	if ((copy = malloc(len + 1)) == NULL) {
		return NULL;
        }
	memcpy(copy, str, len);
	copy[len] = '\0';
	return copy;
}

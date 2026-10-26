// SPDX-License-Identifier: BSD-3-Clause

#include <string.h>

char *strcpy(char *destination, const char *source)
{
	/* TODO: Implement strcpy(). */
	size_t len = 0;
	len = strlen(source);
	for (size_t i = 0; i < len; i++) {
		destination[i] = source[i];
	}
	destination[len] = '\0';
	return destination;
}

char *strncpy(char *destination, const char *source, size_t len)
{
	/* TODO: Implement strncpy(). */
	size_t i = 0;
	for (i = 0; i < len && source[i] != '\0'; i++) {
		destination[i] = source[i];
	}
	while (i < len) {
		destination[i++] = '\0';
	}
	return destination;
}

char *strcat(char *destination, const char *source)
{
	/* TODO: Implement strcat(). */
	size_t len1 = 0;
	size_t len2 = strlen(source);
	char *str = destination;
	while (*str != '\0') {
		str++;
		len1++;
	}
	for (size_t i = 0; i < len2; i++) {
		destination[len1++] = source[i];
	}
	destination[len1] = '\0';
	return destination;
}

char *strncat(char *destination, const char *source, size_t len)
{
	/* TODO: Implement strncat(). */
	size_t len1 = 0;
	char *str = destination;
	while (*str != '\0') {
		str++;
		len1++;
	}
	for (size_t i = 0; i < len; i++) {
		destination[len1++] = source[i];
	}
	destination[len1] = '\0';
	return destination;
}

int strcmp(const char *str1, const char *str2)
{
	/* TODO: Implement strcmp(). */
	size_t i = 0;
	while(str1[i] && str2[i]) {
		if (str1[i] > str2[i])
			return 1;
		else if (str1[i] < str2[i])
			return -1;
		else if (str1[i] == str2[i])
			i++;
	}
	if (str1[i] == '\0' && str2[i] == '\0')
		return 0;
	else if (str1[i] != '\0')
		return 1;
	return -1;
}

int strncmp(const char *str1, const char *str2, size_t len)
{
	/* TODO: Implement strncmp(). */
	size_t i = 0;
	while(*str1 && *str2 && i < len) {
		if (str1[i] > str2[i])
			return 1;
		else if (str1[i] < str2[i])
			return -1;
		else if (str1[i] == str2[i])
			i++;
	}
	if (i == len)
		return 0;
	else if (str1[i] != '\0')
		return 1;
	return -1;
}

size_t strlen(const char *str)
{
	size_t i = 0;

	for (; *str != '\0'; str++, i++)
		;

	return i;
}

char *strchr(const char *str, int c)
{
	/* TODO: Implement strchr(). */
	size_t i = 0;
	while (str[i]) {
		if (str[i] == (char)c) {
			return (char*)(str + i);
		}
		i++;
	}
	return NULL;
}

char *strrchr(const char *str, int c)
{
	/* TODO: Implement strrchr(). */
	size_t i = strlen(str) - 1;
	while (str[i]) {
		if (str[i] == (char)c) {
			return (char*)(str + i);
		}
		i--;
	}
	return NULL;
}

char *strstr(const char *haystack, const char *needle)
{
	/* TODO: Implement strstr(). */
	size_t len1 = strlen(haystack);
	size_t len2 = strlen(needle);
	for (size_t i = 0; i < len1; i++) {
		size_t j = i, k = 0;
		while (haystack[j] == needle[k] && k < len2) {
			j++;
			k++;
		}
		if (k == len2)
			return (char*)(haystack + i);
	}
	return NULL;
}

char *strrstr(const char *haystack, const char *needle)
{
	/* TODO: Implement strrstr(). */
	size_t len1 = strlen(haystack);
	size_t len2 = strlen(needle);
	for (size_t i = len1 - len2; i > 0; i--) {
		size_t j = i, k = 0;
		while (haystack[j] == needle[k] && k < len2) {
			j++;
			k++;
		}
		if (k == len2)
			return (char*)(haystack + i);
	}
	return NULL;
}

void *memcpy(void *destination, const void *source, size_t num)
{
	/* TODO: Implement memcpy(). */
	char* dest = destination;
	char* str = (char*)source;
	for (size_t i = 0; i < num; i++) {
		dest[i] = str[i];
	}
	return destination;
}

void *memmove(void *destination, const void *source, size_t num)
{
	/* TODO: Implement memmove(). */
	char* dest = destination;
	char* str = (char*)source;
	if (dest < str) {
		for (size_t i = 0; i < num; i++) {
			dest[i] = str[i];
		}
	} else if (dest > str) {
		for (size_t i = num - 1, j = 0; j < num; i--, j++) {
			dest[i] = str[i];
		}
	}
	return destination;
}

int memcmp(const void *ptr1, const void *ptr2, size_t num)
{
	/* TODO: Implement memcmp(). */
	char* str1 = (char*)ptr1;
	char* str2 = (char*)ptr2;
	size_t i = 0;
	while(*str1 && *str2 && i < num) {
		if (str1[i] > str2[i])
			return 1;
		else if (str1[i] < str2[i])
			return -1;
		else if (str1[i] == str2[i])
			i++;
	}
	if (i == num)
		return 0;
	else if (str1[i] != '\0')
		return 1;
	return -1;
}

void *memset(void *source, int value, size_t num)
{
	/* TODO: Implement memset(). */
	char* str = source;
	unsigned char val = value;
	for (size_t i = 0; i < num; i++) {
		str[i] = val;
	}
	return source;
}

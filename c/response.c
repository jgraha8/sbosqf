#include <stdio.h>

#include "response.h"

char read_response()
{
	char response[3]={0};

	if( fgets(response, 3, stdin) == NULL ) {
		return -1;
	}

	// Expect newline
	if( response[1] != '\n')
		return -1;

	return *response;
}

#include <string>
#include "radio_hal.h"

int main(int argc, char *argv[])
{
	int i;

	std::string cmd = argv[0];
	for (i = 0; i < argc; i++)
	{
		printf("cmdline param are %s\n", std::string(argv[i]).c_str());
	}

	return 0;
}

#/bin/bash

set -e

gcc filesystem.c -o filesystem

printf("built to to ./filesystem\n");

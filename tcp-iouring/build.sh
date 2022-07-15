cc server.c -o server  -Wall -O2 -D_GNU_SOURCE -luring -std=c++11 -lpthread
cc client.c -o client  -Wall -O2 -D_GNU_SOURCE -luring -std=c++11 -lpthread
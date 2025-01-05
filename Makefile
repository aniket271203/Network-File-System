CFLAGS = -Wall -Wextra -g
CLIENTFILES = client.c utils.c
NAMINGSERVERFILES = NamingServer.c utils.c PathMaps.c SS_Info.c LRU_Cache.c
STORAGESERVERFILES = StorageServer.c utils.c PathHashSS.c ConcurrencySS.c copy.c

all: client NM SS

client:
	gcc $(CFLAGS) $(CLIENTFILES) -o client.out

NM:
	gcc $(CFLAGS) $(NAMINGSERVERFILES) -o NM.out

SS:
	gcc $(CFLAGS) $(STORAGESERVERFILES) -o SS.out

clean:
	rm -f *.out

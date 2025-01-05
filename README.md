# final-project-06
final-project-06 created by GitHub Classroom

# Redundancy
MAJOR CHANGE: NM always connects to an SS once for every request, to check whether it is alive or not. Only then it forwards IP and Port to client.
## Failure Detection:
1. NM needs to detect when a SS goes offline. So, for this, because we connect to SS only when we need to talk to them, we can simply check if connect() times out and errno ETIMEDOUT is set. If this is the case, mark this storage server as "false" for the "isAlive" bool array (corresponding index) of StorageServers struct.
2. So, set connect() as non-blocking, and select() to set a timeout for it, whenever an NM wants to connect to an SS (for ALL operations).

## Data Redundancy and Replication:
1. The first 2 Storage Servers that connect to the Naming Server will be Backup Storage Servers (they are run as `./SS.out b`). Any other storage servers which attempt to be Backup Storage Servers will be rejected immediately.
2. Whenever a new Storage Server is created, and a valid accessible path is given, fork() out and immediately attempt to copy over the file at that path to the first 2 storage servers, at `SS<idx>_0/<given_path>` and `SS<idx>_1/<given_path>`, where idx is the index of the current storage server that is connecting to our Naming Server. Use `copyData()` for this purpose (essentially, `copySrc` and `copyDest`, just the same as a client sending copy command).
3. The first 2 Storage Servers are solely to store backups; no backups of those servers will be created.
4. Only for getinfo and read, we will check if the backup storage servers are alive and have the files. If they are alive, then send the port and IP of the first SS that is alive to the client, so that the client can perform getinfo and read.
5. For getinfo and read, if all storage servers containing the files are dead (including all the backups), then just reject the operation immediately.
6. For EVERY operation except read and getinfo, if the SS containing the path is down, reject the operation and let the client know.

## SS Recovery:
1. The next time we attempt to perform a request on an SS, and we know that it has come back online (if a bool flag signifying that it has gone offline is set because of a connect() timeout as defined earlier), then immediately delete the `SS<idx>_0/...` and `SS<idx>_1/` folders, and copy over all the accessible paths exposed by this storage server to Backup SS 0 and Backup SS 1 immediately.
2. After this, you can perform your original operations.

## Asynchronous Duplication:
1. Every time a create/delete/write/append/copy is called, the NM will immediately fork() out, and create copies of those files/directories at "SS<idx>_0/..." and "SS<idx>_1/..." on Backup SS 0 and 1 respectively.
2. For this purpose, we aren't supposed to wait for an ACK from the storage servers, apparently. (An idea: Maybe, for backup storage servers, they won't send ACK from their end and on the NM end, don't wait for an ACK? i.e., create a new copyData function with the recv removed for the "NM_SS_request_packet".)


# Assumptions
1. The first 2 Storage Servers that connect to the Naming Server will always be Backup Storage Servers (i.e., run "./SS.out b"). They will expose 0 accessible paths of their own to the Naming Server. Any other storage servers which want to be backup storage servers will be rejected immediately.
2. File/directory names shall not consist of newlines ('\n').
3. For creating new files, we do not provide the execute permission to the user.
4. For write and append, client won't enter more than MSG_LEN (a macro defined in headers.h) characters.
5. Directories will return total number of blocks (including hidden files) instead of the total size in bytes.
6. For read, a file will not have more than MSG_LEN (a macro defined in headers.h) characters.
7. A user (both at client and SS end) will add a slash ('/') to the end of the path if they want to operate on/add a directory, and not if it's a file.
8. The user at a SS will only add unique paths.
9. Users will only copy from one directory into another directory that already exists (destination should exist). 
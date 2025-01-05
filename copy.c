#include "copy.h"

// libtar isn't POSIX compliant.
// However, the "zip" and "unzip" commands themselves is POSIX compliant. We shall be
// using this to send our files over. (with execvp()). Reference:
// https://www.unix.com/man-page/posix/1/zip/ and
// https://www.unix.com/man-page/posix/1/unzip/

// Returns 0 if successful.
// Returns -1 if execvp failed.
// Returns -2 if failed to fork.
int execCmd(char* cmd) {
  char* allArgs[100] = {NULL};
  char* token = strtok(cmd, " ");
  int i = 0;
  while (token) {
    for (int j = 0; token[j] != '\0'; j++) {
      if (token[j] == '\a')
        token[j] = ' ';
      else if (token[j] == '\f')
        token[j] = '\t';
      else if (token[j] == '\"' || token[j] == '\'') {  // Pop that char.
        for (int k = j; token[k] != '\0'; k++) token[k] = token[k + 1];
        j--;  // to stay at same value of j for next iteration.
      }
    }
    allArgs[i++] = token;
    token = strtok(NULL, " ");
  }

  int childPID = fork(), status = 0;
  if (childPID < 0) {  // Error while forking
    perror("Failed while attempting to fork parent process");
    return -1;
  } else if (childPID == 0) {  // Child process
    execvp(allArgs[0], allArgs);
    if (errno != 2) perror("Failed to execute the process");
    exit(EXIT_FAILURE);
  } else {  // Parent process
    waitpid(childPID, &status, WUNTRACED);
    if (WEXITSTATUS(status) == EXIT_FAILURE)
      return -1;  // Let main.c know that the execvp failed, probably because such a
                  // command doesn't exist.
    else if (WIFSTOPPED(status))
      return 0;  // Child process has stopped successfully. Let the parent terminal know.
  }
  return 0;
}

// Returns 0 if successful.
// Returns -1 if execvp failed.
// Returns -2 if failed to fork.
// Returns -3 if send failed.
// Returns -4 if failed to open created zip file.
int zipAndSendFile_NMSS(int serverSockFD, char* path) {
  // Make the zip.
  char cmd[2 * PATH_MAX + 12 + 1] = {0};
  sprintf(cmd, "zip -r %s.zip %s", path, path);
  int retVal = execCmd(cmd);
  if (retVal < 0) return retVal;

  // Send the zip.
  memset(cmd, '\0', 2 * PATH_MAX + 12 + 1);
  sprintf(cmd, "%s.zip", path);
  FILE* fd = fopen(cmd, "rb");
  if (!fd) return -4;
  fseek(fd, 0, SEEK_END);
  unsigned long filesize = ftell(fd);
  char temp[20] = {0};
  printf(BCYN "Filesize: %lu\n", filesize);
  sprintf(temp, "%lu", filesize);
  if (send(serverSockFD, temp, strlen(temp), 0) ==
      -1) {  // Send it to the server as well to stop the server.
    perror(BRED "ERROR: Failed to send this message to the server: " CRESET);
    close(serverSockFD);
    return -3;
  }
  rewind(fd);
  char* buffer = (char*)calloc(filesize, sizeof(char));
  fread(buffer, sizeof(char), filesize, fd);
  // printf("Sending data: ");
  // for (int i = 0; i < filesize; i++) printf("%c", buffer[i]);
  if (send(serverSockFD, buffer, filesize, 0) ==
      -1) {  // Send it to the server as well to stop the server.
    perror(BRED "ERROR: Failed to send this message to the server: " CRESET);
    close(serverSockFD);
    return -3;
  }
  free(buffer);

  // Remove the zip.
  memset(cmd, '\0', 2 * PATH_MAX + 12 + 1);
  sprintf(cmd, "rm %s.zip", path);
  retVal = execCmd(cmd);
  if (retVal < 0) return retVal;
  return 0;
}

// Returns 0 if successful.
// Returns -1 if execvp failed.
// Returns -2 if failed to fork.
// Returns -3 if recv failed.
// Returns -4 if failed to open zip file.
int recAndUnzip_NMSS(int clientSockFD, char* path) {
  // Receive the zip.
  char cmd[2 * PATH_MAX + 12 + 1] = {0};
  sprintf(cmd, "%s.zip", path);
  FILE* fd = fopen(cmd, "wb");
  if (!fd) return -4;
  char clientMsg[MAX_BUF_LEN] = {0};
  if (recv(clientSockFD, clientMsg, sizeof(char) * MAX_BUF_LEN, 0) == -1) {
    perror(BRED "ERROR: Failed to receive message from the NM: " CRESET);
    close(clientSockFD);
    return -3;
  }
  unsigned long filesize = strtoul(clientMsg, NULL, 10);
  printf(BBLU "Filesize: %lu\n" CRESET, filesize);
  char* buffer = (char*)calloc(filesize + 1, sizeof(char));
  memset(clientMsg, '\0', sizeof(clientMsg));
  if (recv(clientSockFD, buffer, filesize, 0) == -1) {
    perror(BRED "ERROR: Failed to receive message from the NM: " CRESET);
    close(clientSockFD);
    return -3;
  }

  // printf("Received data: ");
  // for (int i = 0; i < filesize; i++) printf("%c", buffer[i]);

  fwrite(buffer, sizeof(char), filesize, fd);
  fclose(fd);

  // Unzip file.
  memset(cmd, '\0', sizeof(cmd));
  sprintf(cmd, "unzip %s.zip -d %s", path, path);
  int retVal = execCmd(cmd);
  if (retVal < 0) return retVal;

  // Remove the zip.
  memset(cmd, '\0', sizeof(cmd));
  sprintf(cmd, "rm %s.zip", path);
  retVal = execCmd(cmd);
  if (retVal < 0) return retVal;
  return 0;
}
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <vector>

#define CHECK(x) \
    do { \
      if (!(x)) { \
        fputs("CHECK failed: " #x "\n", stderr); \
        abort(); \
      } \
    } while (false)

void PrintResultAndErrno(ssize_t result) {
  if (result < 0) {
    printf("result = %ld, error = %d (%s)\n",
           static_cast<long>(result), errno, strerror(errno));
  } else {
    printf("result = %ld\n", static_cast<long>(result));
  }
}

void DoReceive(int fd) {
  static const size_t kMaxReceiveFDs = 20;

  printf("Receiving a message ...\n");
  char buf[1000] = {};
  struct iovec iov = { buf, sizeof(buf) };
  char cmsg_buf[CMSG_SPACE(sizeof(int) * kMaxReceiveFDs)];
  struct msghdr msg = {};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsg_buf;
  msg.msg_controllen = sizeof(cmsg_buf);

  ssize_t result = recvmsg(fd, &msg, MSG_DONTWAIT);
  PrintResultAndErrno(result);
  if (result < 0)
    return;

  CHECK(msg.msg_controllen != 0);
  struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
  CHECK(cmsg);
  CHECK(cmsg->cmsg_level == SOL_SOCKET);
  CHECK(cmsg->cmsg_type == SCM_RIGHTS);
  CHECK(cmsg->cmsg_len >= CMSG_LEN(0));
  size_t payload_length = cmsg->cmsg_len - CMSG_LEN(0);
  CHECK(payload_length % sizeof(int) == 0);
  size_t num_received_fds = payload_length / sizeof(int);
  printf("Received %lu FDs ...\n",
         static_cast<unsigned long>(num_received_fds));
  const int* received_fds = reinterpret_cast<int*>(CMSG_DATA(cmsg));
  CHECK(num_received_fds >= 1);
  CHECK(close(received_fds[0]) == 0);
}

int main(int argc, char** argv) {
  int fds[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
  CHECK(fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0);
  CHECK(fcntl(fds[1], F_SETFL, O_NONBLOCK) == 0);

  int num = 1000000;
  if (argc >= 2)
    num = atoi(argv[1]);
  CHECK(num >= 0);

  for (int i = 0; i < num; i++) {
    static const size_t kNumFDs = 1;

    char buf[CMSG_SPACE(kNumFDs * sizeof(int))];
    // Note: The sendmsg() below *always* fails on Mac if we don't write at
    // least one character.
    struct iovec iov = { const_cast<char*>("x"), 1 };
    struct msghdr msg = {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * kNumFDs);
    int new_fd = dup(fds[0]);
    CHECK(new_fd != -1);
    CHECK(new_fd != fds[0]);
    reinterpret_cast<int*>(CMSG_DATA(cmsg))[0] = new_fd;
    msg.msg_controllen = cmsg->cmsg_len;

    int flags = 0;
#ifndef __APPLE__
    flags |= MSG_NOSIGNAL;
#endif
    ssize_t result = sendmsg(fds[0], &msg, flags);
    CHECK(result <= 1);
    if (result != 1) {
      printf("Failed sendmsg (with FD) at i = %d\n", i);
      PrintResultAndErrno(result);

      // Can send another?
      struct iovec iov = { const_cast<char*>("x"), 1 };
      struct msghdr msg = {};
      msg.msg_iov = &iov;
      msg.msg_iovlen = 1;
      int flags = 0;
#ifndef __APPLE__
      flags |= MSG_NOSIGNAL;
#endif
      printf("Send another with no FDs ... \n");
      result = sendmsg(fds[0], &msg, flags);
      PrintResultAndErrno(result);

      DoReceive(fds[1]);

      return 1;
    }

    close(new_fd);
  }

  return 0;
}
